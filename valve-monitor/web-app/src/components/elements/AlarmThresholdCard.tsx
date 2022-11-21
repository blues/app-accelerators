import { Card, Form, Input } from "antd";
import { updateAlarmThreshold } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";
// import monitorFrequencyCardStyles from "../../styles/MonitorFrequencyCard.module.scss";

export interface AlarmThresholdProps {
  currentMinFlowThreshold: number;
  currentMaxFlowThreshold: number;
  setCurrentMinFlowThreshold: (currentMinFlow: number) => void;
  setCurrentMaxFlowThreshold: (currentMaxFlow: number) => void;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
}

const AlarmThresholdCard = (props: AlarmThresholdProps) => {
  const {
    currentMinFlowThreshold,
    currentMaxFlowThreshold,
    setCurrentMinFlowThreshold,
    setCurrentMaxFlowThreshold,
    setIsErrored,
    setIsLoading,
    setErrorMessage,
  } = props;

  const [form] = Form.useForm();

  const update = async () => {
    const min = Number(form.getFieldValue("min"));
    const max = Number(form.getFieldValue("max"));

    console.log(currentMinFlowThreshold, currentMaxFlowThreshold);
    console.log(min, max);
    console.log("---");

    if (min === currentMinFlowThreshold && max === currentMaxFlowThreshold) {
      return;
    }

    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateAlarmThreshold(Number(min), Number(max));
      setCurrentMinFlowThreshold(Number(min));
      setCurrentMaxFlowThreshold(Number(max));
    } catch (e) {
      setIsErrored(true);
      // TODO: Update this message
      setErrorMessage(ERROR_MESSAGE.UPDATE_FLEET_MONITOR_FREQUENCY_FAILED);
    }
    setIsLoading(false);
  };

  return (
    <Card
      className={cardStyles.cardContainer}
      title={
        <div className={cardStyles.cardTitle}>
          <div>Alarm Threshold</div>
        </div>
      }
    >
      <div className="">
        <p>Configure default flow rate range</p>
        <Form
          form={form}
          initialValues={{
            min: currentMinFlowThreshold,
            max: currentMaxFlowThreshold,
          }}
          onFinish={update}
        >
          <Form.Item name="min" label="Min mL/min">
            <Input
              placeholder="xx.x"
              onBlur={form.submit}
              onPressEnter={form.submit}
            />
          </Form.Item>
          <Form.Item name="max" label="Max mL/min">
            <Input
              placeholder="xx.x"
              onBlur={form.submit}
              onPressEnter={form.submit}
            />
          </Form.Item>
        </Form>
      </div>
    </Card>
  );
};

export default AlarmThresholdCard;
