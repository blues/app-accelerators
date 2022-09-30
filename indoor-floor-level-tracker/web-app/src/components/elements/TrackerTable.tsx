import React, { useEffect, useRef, useState } from "react";
import Image from "next/image";
import { Form, Input, InputRef, Table } from "antd";
import { ArrowDownOutlined, ArrowUpOutlined } from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import { DeviceTracker } from "../../services/AppModel";
import { changeDeviceName } from "../../api-client/devices";
import { ERROR_MESSAGE } from "../../constants/ui";
import ResponderIcon from "../elements/images/responder.svg";
import styles from "../../styles/Table.module.scss";

const columns = [
  {
    title: (
      <>
        <Image src={ResponderIcon} alt="person outline" />
        <span style={{ marginLeft: "8px" }}>Responders</span>
      </>
    ),
    dataIndex: "name",
    key: "name",
    width: "25%",
  },
  {
    title: "Floor",
    dataIndex: "floor",
    key: "floor",
    width: "15%",
  },
  {
    title: "Last Seen",
    dataIndex: "lastActivity",
    key: "lastActivity",
  },
  {
    title: "Pressure",
    dataIndex: "pressure",
    key: "pressure",
    width: "20%",
  },
  {
    title: "Voltage",
    dataIndex: "voltage",
    key: "voltage",
    width: "15%",
  },
] as ColumnsType<DeviceTracker>;

const CustomRow = (props) => {
  return <>{props.children}</>;
};

interface CustomCellProps {
  index: string;
  children: JSX.Element;
  record: DeviceTracker;
  onChange: (deviceUID: string, updatedName: string) => Promise<boolean>;
}

const CustomCell = ({ children, index, record, onChange }: CustomCellProps) => {
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

    onChange(record.uid, newName).then(toggleEdit).catch(toggleEdit);
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

  // Create a custom form for editing responder name
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

  // Custom display of floor number
  if (index === "floor" && record.floor) {
    const directionAsNumber = record.direction
      ? parseInt(record.direction, 10)
      : 0;

    childNode = (
      <>
        {directionAsNumber > 0 && <ArrowUpOutlined />}
        {directionAsNumber < 0 && <ArrowDownOutlined />}
        <span>{record.floor}</span>
      </>
    );
  }

  return <td>{childNode}</td>;
};

interface TrackerTableProps {
  data: DeviceTracker[] | undefined;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
  refreshData: () => Promise<void>;
}

const TrackerTable = ({
  data,
  refreshData,
  setIsErrored,
  setIsLoading,
  setErrorMessage,
}: TrackerTableProps) => {
  const onTrackerNameChange = async (deviceUID: string, newName: string) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await changeDeviceName(deviceUID, newName);
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.DEVICE_NAME_CHANGE_FAILED);
    }

    await refreshData();
    setIsLoading(false);
  };

  const editableColumns = columns.map((col) => ({
    ...col,
    onCell: (record: DeviceTracker) => ({
      record,
      index: col.key,
      title: col.title,
      onChange: onTrackerNameChange,
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
        rowKey="uid"
        rowClassName={(record) => {
          return record.lastAlarm ? "alarm" : "";
        }}
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

export default TrackerTable;
