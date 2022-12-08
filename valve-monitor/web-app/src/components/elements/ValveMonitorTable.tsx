import { MutableRefObject, useEffect, useRef, useState } from "react";
import {
  Form,
  Input,
  InputNumber,
  InputRef,
  Switch,
  Table,
  Tag,
  Tooltip,
} from "antd";
import type { ColumnsType } from "antd/lib/table";
import { isEmpty } from "lodash";
import { ValveMonitorDevice } from "../../services/AppModel";
import { ERROR_MESSAGE, getValveStateAlarmMessage } from "../../constants/ui";
import {
  changeDeviceName,
  updateDeviceValveMonitorConfig,
  updateValveControl,
} from "../../api-client/valveDevices";
import styles from "../../styles/ValveMonitorTable.module.scss";
import {
  WarningFilled,
  ArrowUpOutlined,
  ArrowDownOutlined,
} from "@ant-design/icons";

const columns = [
  {
    title: "Location",
    dataIndex: "name",
    key: "name",
    editable: true,
  },
  {
    title: (
      <>
        <div>Flow Rate</div>
        <div>mL/min</div>
      </>
    ),
    dataIndex: "deviceFlowRate",
    key: "deviceFlowRate",
    align: "center",
    render: (_, record) => (
      <>
        <span className={styles.rowTitle}>Flow Rate mL/min</span>
        <span>{record.deviceFlowRate}</span>
      </>
    ),
  },
  {
    title: "Monitoring (min)",
    dataIndex: "monitorFrequency",
    key: "monitorFrequency",
    editable: true,
    align: "center",
  },
  {
    title: "Alarm Setting",
    children: [
      {
        title: "Min",
        dataIndex: "minFlowThreshold",
        editable: true,
        key: "minFlowThreshold",
        align: "center",
      },
      {
        title: "Max",
        dataIndex: "maxFlowThreshold",
        editable: true,
        key: "maxFlowThreshold",
        align: "center",
      },
    ],
    dataIndex: "deviceAlarmThreshold",
    key: "deviceAlarmThreshold",
    align: "center",
  },
  {
    title: "Alarm",
    dataIndex: "deviceAlarm",
    key: "deviceAlarm",
    align: "center",
    render(value, record) {
      return (
        <>
          <span className={styles.rowTitle}>Alarm</span>
          <span>
            {value ? (
              <Tooltip
                title={getValveStateAlarmMessage(record.deviceAlarmReason)}
              >
                {record.deviceAlarmReason === "low" && <ArrowDownOutlined />}
                {record.deviceAlarmReason === "high" && <ArrowUpOutlined />}
                <WarningFilled />
              </Tooltip>
            ) : (
              <>-</>
            )}
          </span>
        </>
      );
    },

    /*
      <>
        <span className={styles.rowTitle}>Alarm</span>
        <span>{record.deviceAlarm}</span>
      </>
      */
  },
  {
    title: (
      <>
        <div>Valve</div>
        <div>State</div>
      </>
    ),
    render: (_, record) => (
      <>
        <span className={styles.rowTitle}>Valve State</span>
        <Tag color={record.valveState === "open" ? "success" : "warning"}>
          {record.valveState}
        </Tag>
      </>
    ),
    align: "center",
  },
  {
    title: (
      <>
        <div>Valve Control</div>
        <div>(open / closed)</div>
      </>
    ),
    align: "center",
    key: "valveControl",
    editable: true,
  },
] as ColumnsType<ValveMonitorDevice>;

interface EditableCellProps {
  title: string | { props: { children: any } };
  editable: boolean;
  index: string;
  children: JSX.Element;
  record: ValveMonitorDevice;
  onChange: (
    deviceUID: string,
    valveDeviceEnvVarToUpdate: { [key: string]: any }
  ) => Promise<boolean>;
}

const EditableCell = ({
  title,
  editable,
  children,
  index,
  record,
  onChange,
}: EditableCellProps) => {
  const [editing, setEditing] = useState(false);
  const inputRef = useRef<HTMLInputElement | InputRef | null>(null);
  const [form] = Form.useForm();

  useEffect(() => {
    if (editing && inputRef && inputRef.current) {
      inputRef.current.focus();
    }
  }, [editing]);

  const toggleEdit = () => {
    setEditing(!editing);
    form.setFieldsValue({ [index]: record[index] });
  };

  const handleSave = () => {
    const valveDeviceEnvVarToUpdate = form.getFieldsValue([index]) as {
      [key: string]: number;
    };

    if (record[index] === valveDeviceEnvVarToUpdate[index]) {
      toggleEdit();
      return;
    }

    onChange(record.deviceID, valveDeviceEnvVarToUpdate)
      .then(toggleEdit)
      .catch(toggleEdit);
  };

  const handleBlur = () => {
    const valveDeviceEnvVarToUpdate = form.getFieldsValue([index]) as {
      [key: string]: number;
    };

    if (isEmpty(valveDeviceEnvVarToUpdate[index])) {
      toggleEdit();
      return;
    }

    handleSave();
  };

  let childNode = children;

  let rowTitle = title;
  if (title === "Min") {
    rowTitle = "Alarm Setting Minimum";
  }
  if (title === "Max") {
    rowTitle = "Alarm Setting Maximum";
  }
  if (
    typeof title !== "string" &&
    title.props &&
    title.props.children &&
    title?.props?.children[1].props.children === "(open / closed)"
  ) {
    rowTitle = "Valve Control (open/closed)";
  }
  if (
    typeof title !== "string" &&
    title.props &&
    title.props.children &&
    title?.props?.children[0].props.children === "Monitoring"
  ) {
    rowTitle = "Monitoring (min)";
  }

  // Create a custom form for editing cells
  if (editable && index !== "valveControl") {
    childNode = editing ? (
      <>
        <span className={styles.rowTitle}>{rowTitle}</span>
        <Form form={form}>
          <Form.Item
            style={{
              margin: 0,
            }}
            name={index}
            rules={[
              {
                required: true,
                message:
                  index === "name" ? `Name is required` : `Number is required`,
              },
            ]}
            initialValue={record[index] as [key: string]}
          >
            {index === "name" ? (
              <Input
                className="editable-input editable-input-name"
                onBlur={handleBlur}
                onPressEnter={handleSave}
                ref={inputRef as MutableRefObject<InputRef>}
              />
            ) : (
              <InputNumber
                className="editable-input"
                min="0"
                placeholder="xx.x"
                onBlur={handleBlur}
                onPressEnter={handleSave}
                ref={inputRef as MutableRefObject<HTMLInputElement>}
              />
            )}
          </Form.Item>
        </Form>
      </>
    ) : (
      <>
        <span className={styles.rowTitle}>{rowTitle}</span>
        <button
          className={`editable-button editable-button-${index}`}
          onClick={toggleEdit}
          type="button"
        >
          {children[1] || "xx.x"}
        </button>
      </>
    );
  }

  if (index === "valveControl") {
    childNode =
      record.valveState !== "-" ? (
        <>
          <span className={styles.rowTitle}>Valve Control (open/closed)</span>
          <Switch
            onChange={(value) => {
              onChange(record.deviceID, {
                valveControl: value ? "open" : "close",
              });
            }}
            loading={
              record.valveState === "opening" || record.valveState === "closing"
            }
            checked={
              record.valveState === "open" || record.valveState === "opening"
            }
          />
        </>
      ) : (
        <>
          <span className={styles.rowTitle}>Valve Control (open/closed)</span>-
        </>
      );
  }

  return (
    // Providing the colors in JavaScript to override the Ant hover styling
    // Errored rows should stay red on hover.
    <td style={{ backgroundColor: record.deviceAlarm ? "#fbe9e7" : "white" }}>
      {childNode}
    </td>
  );
};

interface ValveMonitorTableProps {
  data: ValveMonitorDevice[] | undefined;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
  refreshData: () => Promise<void>;
}

interface ValveMonitorConfigChange {
  name?: string;
  monitorFrequency?: string;
  minFlowThreshold?: string;
  maxFlowThreshold?: string;
  valveControl?: string;
}

const ValveMonitorTable = ({
  data,
  refreshData,
  setIsErrored,
  setIsLoading,
  setErrorMessage,
}: ValveMonitorTableProps) => {
  const onDeviceValveMonitorConfigChange = async (
    deviceUID: string,
    valveDeviceEnvVarToUpdate: ValveMonitorConfigChange
  ) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      if (valveDeviceEnvVarToUpdate.name) {
        // Name updates
        await changeDeviceName(deviceUID, valveDeviceEnvVarToUpdate.name);
      } else if (valveDeviceEnvVarToUpdate.valveControl) {
        // Valve control updates
        await updateValveControl(
          deviceUID,
          valveDeviceEnvVarToUpdate.valveControl
        );
      } else {
        // All other environment variable updates
        await updateDeviceValveMonitorConfig(
          deviceUID,
          valveDeviceEnvVarToUpdate
        );
      }
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.UPDATE_DEVICE_CONFIG_FAILED);
    }

    setIsLoading(false);
    await refreshData();
  };

  const mapColumns = (col) => {
    const newCol = {
      ...col,
      onCell: (record: ValveMonitorDevice) => ({
        title: col.title,
        editable: col.editable,
        index: col.key,
        record,
        onChange: onDeviceValveMonitorConfigChange,
      }),
    } as EditableCellProps;
    if (col.children) {
      newCol.children = col.children.map(mapColumns);
    }
    return newCol;
  };

  const editableColumns = columns.map(
    mapColumns
  ) as ColumnsType<ValveMonitorDevice>;

  return (
    <div className={styles.tableContainer}>
      <Table
        components={{
          body: {
            cell: EditableCell,
          },
        }}
        rowKey="deviceID"
        columns={editableColumns}
        dataSource={data}
        pagination={false}
        size="small"
      />
    </div>
  );
};
export default ValveMonitorTable;
