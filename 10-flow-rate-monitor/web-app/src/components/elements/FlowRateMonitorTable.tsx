import { MutableRefObject, useEffect, useRef, useState } from "react";
import { Form, Input, InputNumber, InputRef, Table, Tooltip } from "antd";
import type { ColumnsType } from "antd/lib/table";
import { isEmpty } from "lodash";
import {
  WarningFilled,
  ArrowUpOutlined,
  ArrowDownOutlined,
} from "@ant-design/icons";
import { FlowRateMonitorDevice } from "../../services/AppModel";
import {
  ERROR_MESSAGE,
  getFlowRateStateAlarmMessage,
} from "../../constants/ui";
import {
  changeDeviceName,
  updateDeviceFlowRateMonitorConfig,
} from "../../api-client/flowRateDevices";
import styles from "../../styles/FlowRateMonitorTable.module.scss";

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
    title: (
      <>
        <div>Monitoring</div>
        <div>(min)</div>
      </>
    ),
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
                title={getFlowRateStateAlarmMessage(record.deviceAlarmReason)}
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
  },
] as ColumnsType<FlowRateMonitorDevice>;

interface EditableCellProps {
  title: string | { props: { children: any } };
  editable: boolean;
  index: string;
  children: JSX.Element;
  record: FlowRateMonitorDevice;
  onChange: (
    deviceUID: string,
    flowRateDeviceEnvVarToUpdate: { [key: string]: any }
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
    const flowRateDeviceEnvVarToUpdate = form.getFieldsValue([index]) as {
      [key: string]: number;
    };

    if (record[index] === flowRateDeviceEnvVarToUpdate[index]) {
      toggleEdit();
      return;
    }

    onChange(record.deviceID, flowRateDeviceEnvVarToUpdate)
      .then(toggleEdit)
      .catch(toggleEdit);
  };

  const handleBlur = () => {
    const flowRateDeviceEnvVarToUpdate = form.getFieldsValue([index]) as {
      [key: string]: number;
    };

    if (isEmpty(flowRateDeviceEnvVarToUpdate[index])) {
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
    title?.props?.children[0].props.children === "Monitoring"
  ) {
    rowTitle = "Monitoring (min)";
  }

  // Create a custom form for editing cells
  if (editable) {
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

  return (
    // Providing the colors in JavaScript to override the Ant hover styling
    // Errored rows should stay red on hover.
    <td style={{ backgroundColor: record.deviceAlarm ? "#fbe9e7" : undefined }}>
      {childNode}
    </td>
  );
};

interface FlowRateMonitorTableProps {
  data: FlowRateMonitorDevice[] | undefined;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
  refreshData: () => Promise<void>;
}

interface FlowRateMonitorConfigChange {
  name?: string;
  monitorFrequency?: string;
  minFlowThreshold?: string;
  maxFlowThreshold?: string;
}

const FlowRateMonitorTable = ({
  data,
  refreshData,
  setIsErrored,
  setIsLoading,
  setErrorMessage,
}: FlowRateMonitorTableProps) => {
  const onDeviceFlowRateMonitorConfigChange = async (
    deviceUID: string,
    flowRateDeviceEnvVarToUpdate: FlowRateMonitorConfigChange
  ) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      if (flowRateDeviceEnvVarToUpdate.name) {
        // Name updates
        await changeDeviceName(deviceUID, flowRateDeviceEnvVarToUpdate.name);
      } else {
        // All other environment variable updates
        await updateDeviceFlowRateMonitorConfig(
          deviceUID,
          flowRateDeviceEnvVarToUpdate
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
      onCell: (record: FlowRateMonitorDevice) => ({
        title: col.title,
        editable: col.editable,
        index: col.key,
        record,
        onChange: onDeviceFlowRateMonitorConfigChange,
      }),
    } as EditableCellProps;
    if (col.children) {
      newCol.children = col.children.map(mapColumns);
    }
    return newCol;
  };

  const editableColumns = columns.map(
    mapColumns
  ) as ColumnsType<FlowRateMonitorDevice>;

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
export default FlowRateMonitorTable;
