import React, { useEffect, useRef, useState } from "react";
import Image from "next/image";
import { Form, Input, InputRef, Table, Tooltip } from "antd";
import {
  ArrowDownOutlined,
  ArrowUpOutlined,
  WarningOutlined,
} from "@ant-design/icons";
import type { ColumnsType } from "antd/es/table";
import { DeviceTracker } from "../../services/AppModel";
import { services } from "../../services/ServiceLocatorClient";
import { changeDeviceName } from "../../api-client/devices";
import { ERROR_MESSAGE } from "../../constants/ui";
import ResponderIcon from "./images/responder.svg";
import styles from "../../styles/TrackerTable.module.scss";

const alarmService = services().getAlarmService();
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
  },
  {
    title: "Floor",
    dataIndex: "floor",
    key: "floor",
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
    responsive: ["md"],
  },
  {
    title: "Voltage",
    dataIndex: "voltage",
    key: "voltage",
    responsive: ["sm"],
  },
] as ColumnsType<DeviceTracker>;

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
  const isAlarmActive =
    !!record.lastAlarm &&
    parseInt(record.lastAlarm, 10) >
      parseInt(alarmService.getLastAlarmClear(), 10);

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
  const getCellIcon = () => {
    const directionAsNumber = record.direction
      ? parseInt(record.direction, 10)
      : 0;
    if (isAlarmActive) {
      return (
        <Tooltip title="This responder has not changed floors recently.">
          <WarningOutlined />
        </Tooltip>
      );
    }
    if (directionAsNumber > 0) {
      return <ArrowUpOutlined />;
    }
    if (directionAsNumber < 0) {
      return <ArrowDownOutlined />;
    }
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
  if (index === "floor") {
    childNode = (
      <>
        {getCellIcon()}
        <span>{record.floor}</span>
      </>
    );
  }

  return (
    // Providing the colors in JavaScript to override the Ant hover styling
    // Errored rows should stay red on hover.
    <td style={{ backgroundColor: isAlarmActive ? "#fbe9e7" : "white" }}>
      {childNode}
    </td>
  );
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
