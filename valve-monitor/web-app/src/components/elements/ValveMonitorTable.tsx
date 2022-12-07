import { MutableRefObject, useEffect, useRef, useState } from "react";
import {
  Form,
  Input,
  InputNumber,
  InputRef,
  Spin,
  Switch,
  Table,
  Tag,
} from "antd";
import type { ColumnsType } from "antd/lib/table";
import { isEmpty } from "lodash";
import { ValveMonitorDevice } from "../../services/AppModel";
import { ERROR_MESSAGE } from "../../constants/ui";
import {
  changeDeviceName,
  updateDeviceValveMonitorConfig,
  updateValveControl,
} from "../../api-client/valveDevices";
import styles from "../../styles/ValveMonitorTable.module.scss";
import { LoadingOutlined } from "@ant-design/icons";

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
  },
  {
    title: "Valve State",
    render: (_, record) => (
      <Tag color={record.valveState === "open" ? "success" : "warning"}>
        {record.valveState}
      </Tag>
    ),
    align: "center",
  },
  {
    title: "Valve Control (open/closed)",
    align: "center",
    width: "15%",
    key: "valveControl",
    editable: true,
  },
] as ColumnsType<ValveMonitorDevice>;

interface EditableCellProps {
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

  // Create a custom form for editing cells
  if (editable && index !== "valveControl") {
    childNode = editing ? (
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
              placeholder="xx.x"
              onBlur={handleBlur}
              onPressEnter={handleSave}
              ref={inputRef as MutableRefObject<HTMLInputElement>}
            />
          )}
        </Form.Item>
      </Form>
    ) : (
      <button
        className={`editable-button editable-button-${index}`}
        onClick={toggleEdit}
        type="button"
      >
        {children[1] || "xx.x"}
      </button>
    );
  }

  if (index === "valveControl") {
    childNode =
      record.valveState !== "-" ? (
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
      ) : (
        <>-</>
      );
  }

  return <td>{childNode}</td>;
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
      />
    </div>
  );
};
export default ValveMonitorTable;
