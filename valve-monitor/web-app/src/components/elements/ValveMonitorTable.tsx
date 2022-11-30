import { useEffect, useRef, useState } from "react";
import { Form, Input, InputRef, Switch, Table, Tag } from "antd";
import type { ColumnsType } from "antd/lib/table";
import { isEmpty } from "lodash";
import { ValveMonitorDevice } from "../../services/AppModel";
import { ERROR_MESSAGE } from "../../constants/ui";
import { updateDeviceValveMonitorConfig } from "../../api-client/valveDevices";
import styles from "../../styles/ValveMonitorTable.module.scss";

const columns = [
  {
    title: "Valve Location",
    dataIndex: "name",
    key: "name",
  },
  {
    title: (
      <>
        <div>Flow Rate</div>
        <div> mL/min</div>
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
        <div>Frequency (min)</div>
      </>
    ),
    dataIndex: "monitorFrequency",
    key: "monitorFrequency",
    editable: true,
    align: "center",
  },
  {
    title: "Alarm Threshold",
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
    render: (_, record) => <Switch checked={record.valveState === "open"} />,
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
  const inputRef = useRef<InputRef | null>(null);
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
  if (editable) {
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
              message: `Number is required`,
            },
          ]}
          initialValue={record[index] as [key: string]}
        >
          <Input
            className="editable-input"
            onBlur={handleBlur}
            onPressEnter={handleSave}
            ref={inputRef}
          />
        </Form.Item>
      </Form>
    ) : (
      <button className="editable-button" onClick={toggleEdit} type="button">
        {children}
      </button>
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

const ValveMonitorTable = ({
  data,
  refreshData,
  setIsErrored,
  setIsLoading,
  setErrorMessage,
}: ValveMonitorTableProps) => {
  const onDeviceValveMonitorConfigChange = async (
    deviceUID: string,
    valveDeviceEnvVarToUpdate: object
  ) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateDeviceValveMonitorConfig(
        deviceUID,
        valveDeviceEnvVarToUpdate
      );
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.UPDATE_DEVICE_MONITOR_FREQUENCY_FAILED);
    }

    await refreshData();
    setIsLoading(false);
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
