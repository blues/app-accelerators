import { Form, Input, InputRef, Switch, Table, Tag } from "antd";
import type { ColumnsType } from "antd/lib/table";
import { useEffect, useRef, useState } from "react";
import { changeDeviceName } from "../../api-client/valveDevices";
import { ERROR_MESSAGE } from "../../constants/ui";
import { ValveMonitorDevice } from "../../services/AppModel";
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
    align: "center",
  },
  {
    title: "Alarm Threshold",
    colSpan: 2,
    children: [
      {
        title: "Min",
        dataIndex: "minFlowThreshold",
        colSpan: 0,
        align: "center",
        onCell: () => ({ rowSpan: 1 }),
      },
      {
        title: "Max",
        dataIndex: "maxFlowThreshold",
        colSpan: 0,
        align: "center",
        onCell: () => ({ rowSpan: 1 }),
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

interface CustomCellProps {
  index: string;
  children: JSX.Element;
  record: ValveMonitorDevice;
  onNameChange: (deviceID: string, updatedName: string) => Promise<boolean>;
}

const CustomCell = ({
  children,
  index,
  record,
  onNameChange,
}: CustomCellProps) => {
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
  };
  const handleSave = () => {
    const newName = form.getFieldValue("name") as string;
    if (newName.trim() === "") {
      return;
    }

    if (record.name === newName) {
      toggleEdit();
      return;
    }

    onNameChange(record.deviceID, newName).then(toggleEdit).catch(toggleEdit);
  };
  const handleBlur = () => {
    const newName = form.getFieldValue("name") as string;
    if (newName.trim() === "") {
      toggleEdit();
      return;
    }
    handleSave();
  };

  let childNode = children;
  if (index === "name") {
    childNode = editing ? (
      <Form form={form}>
        <Form.Item
          style={{
            margin: 0,
          }}
          name="name"
          rules={[
            {
              required: true,
              message: "Name is required",
            },
          ]}
          initialValue={record.name}
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
  const onNameChange = async (deviceID: string, newName: string) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await changeDeviceName(deviceID, newName);
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED);
    }

    await refreshData();
    setIsLoading(false);
  };

  const editableColumns = columns.map((col) => ({
    ...col,
    onCell: (record: ValveMonitorDevice) => ({
      record,
      index: col.key,
      title: col.title,
      onNameChange,
    }),
  }));

  return (
    <div className={styles.tableContainer}>
      <Table
        components={{
          body: {
            cell: CustomCell,
          },
        }}
        rowKey="name"
        // TypeScript does not like the custom properties being present on the column
        // definitions, but they’re essential to Ant’s recommended way of allowing
        // users to edit cell contents.
        // https://ant.design/components/table/#components-table-demo-edit-cell
        // https://github.com/ant-design/ant-design/issues/22451#issuecomment-714513684
        // @ts-ignore
        columns={editableColumns}
        dataSource={data}
        pagination={false}
      />
    </div>
  );
};
export default ValveMonitorTable;
