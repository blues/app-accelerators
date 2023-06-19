#include "actuator.h"

#include <stdlib.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <note.h>
#include "note_c_hooks.h"

// Actuator pin
#define ACTUATOR_NODE DT_ALIAS(actuator)
#if !DT_NODE_HAS_STATUS(ACTUATOR_NODE, okay)
#error "Unsupported board: actuator devicetree alias is not defined"
#endif

static const struct gpio_dt_spec actuator = GPIO_DT_SPEC_GET_OR(ACTUATOR_NODE, gpios, {0});

// ATTN pin
#define ATTN_NODE DT_ALIAS(attn)
#if !DT_NODE_HAS_STATUS(ATTN_NODE, okay)
#error "Unsupported board: attn devicetree alias is not defined"
#endif

static const struct gpio_dt_spec attn = GPIO_DT_SPEC_GET_OR(ATTN_NODE, gpios, {0});

static struct gpio_callback attnIsrData;
static const char queue_file[] = "actuator.qi";

// Function declaration(s)
static void attnArm(void);
static void attnWorkCb(struct k_work *);
static void attnIsr(const struct device *, struct gpio_callback *, uint32_t);
static int configureGpio(void);
static void deleteNote(J *note_);
static J *dequeueCommand(bool pop_);
static J *emptyNotecardQueue(void);

// Arm the Notecard's ATTN interrupt.
static void attnArm(void)
{
    // Once ATTN has triggered, it stays set until explicitly rearmed. Rearm it
    // here. It will trigger again after a change to the watched Notefile.
    J *req = NoteNewRequest("card.attn");
    if (req)
    {
        JAddStringToObject(req, "mode", "rearm,files");
        J *files = JAddArrayToObject(req, "files");
        if (files)
        {
            JAddItemToArray(files, JCreateString(queue_file));
            NoteRequest(req);
        }
        else
        {
            NoteDeleteResponse(req);
        }
    }
}

static void attnWorkCb(struct k_work *)
{
    // Drain `actuator.qi`
    J *note = emptyNotecardQueue();

    // Inspect last Note
    if (note)
    {
        // Evaluate Note
        deleteNote(note);

        // Actuate
        gpio_pin_toggle_dt(&actuator);
        printk("Actuator: ON\n");
        NoteDelayMs(1000);
        gpio_pin_toggle_dt(&actuator);
        printk("Actuator: OFF\n");
    }

    // Rearm
    attnArm();
}

K_WORK_DEFINE(attnWorkItem, attnWorkCb);

static void attnIsr(const struct device *, struct gpio_callback *, uint32_t)
{
    k_work_submit(&attnWorkItem);
}

// Configure the GPIO pins used by the actuator.
static int configureGpio(void)
{
    int result;

    // ATTN pin setup
    if (!device_is_ready(attn.port))
    {
        printk("Error: ATTN GPIO device %s is not ready\n", attn.port->name);
        return -1;
    }

    // Configure ATTN pin as input
    result = gpio_pin_configure_dt(&attn, GPIO_INPUT);
    if (result != 0)
    {
        printk("Error %d: failed to configure %s pin %d\n",
               result, attn.port->name, attn.pin);
        return result;
    }

    // Configure ATTN pin interrupt to trigger on rising edge
    result = gpio_pin_interrupt_configure_dt(&attn, GPIO_INT_EDGE_TO_ACTIVE);
    if (result != 0)
    {
        printk("Error %d: failed to configure interrupt on %s pin %d\n",
               result, attn.port->name, attn.pin);
        return result;
    }

    // Register ATTN pin interrupt callback
    gpio_init_callback(&attnIsrData, attnIsr, BIT(attn.pin));
    gpio_add_callback(attn.port, &attnIsrData);

    printk("Set up ATTN at %s pin %d\n", attn.port->name, attn.pin);

    // Actuator pin setup
    if (!device_is_ready(actuator.port))
    {
        printk("Error: Valve GPIO device %s is not ready\n", actuator.port->name);
        return -1;
    }

    // Configure actuator pin as output
    result = gpio_pin_configure_dt(&actuator, GPIO_OUTPUT_INACTIVE);
    if (result != 0)
    {
        printk("Error %d: failed to configure %s pin %d\n",
               result, actuator.port->name, actuator.pin);
        return result;
    }

    printk("Set up actuator at %s pin %d\n", actuator.port->name, actuator.pin);

    return result;
}

static void deleteNote(J *note_)
{
    if (!note_)
    {
        return;
    }

    char *msg = JConvertToJSONString(note_);
    printk("Deleted message with contents:\n\t> %s\n", msg);
    free(msg);
    NoteDeleteResponse(note_);
}

static J *dequeueCommand(bool pop_)
{
    J *rsp = NULL;

    J *req = NoteNewRequest("note.get");
    if (req)
    {
        JAddStringToObject(req, "file", queue_file);
        JAddBoolToObject(req, "delete", pop_);
        rsp = NoteRequestResponse(req);
    }

    return rsp;
}

static J *emptyNotecardQueue(void)
{
    J *rsp = NULL;
    J *prev_rsp = NULL;

    for (bool empty = false; !empty;)
    {
        rsp = dequeueCommand(true);
        if (rsp)
        {
            if (NoteResponseError(rsp))
            {
                deleteNote(rsp);
                empty = true;
            }
            else if (prev_rsp)
            {
                deleteNote(prev_rsp);
            }
            else
            {
                prev_rsp = rsp;
            }
        }
        else
        {
            printk("ERROR: Notecard communication error!\n");
            NoteDelayMs(250);
        }
    }

    return prev_rsp;
}

void initActuator(void)
{
    configureGpio();
    attnArm();
}
