import React, { useEffect, useRef, useState } from "react";
import { Form, Input, InputRef, Table } from "antd";
import type { ColumnsType } from "antd/es/table";
import { DeviceTracker } from "../../services/ClientModel";
import styles from "../../styles/Table.module.scss";

const columns = [
  {
    title: "Responders",
    dataIndex: "name",
    key: "name",
  },
  {
    title: "Floor",
    dataIndex: "floor",
    key: "floor",
  },
  {
    title: "Alerts",
    dataIndex: "alerts",
    key: "alerts",
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
  },
  {
    title: "Voltage",
    dataIndex: "voltage",
    key: "voltage",
  },
] as ColumnsType<DeviceTracker>;

interface EditableCellProps {
  title: string;
  children: JSX.Element;
  record: DeviceTracker;
  onChange: (deviceUID: string, updatedName: string) => Promise<boolean>;
}

const EditableCell = ({
  children,
  title,
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
  };
  const save = () => {
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
  const onBlur = () => {
    const newName = form.getFieldValue("name") as string;
    if (newName.trim() === "") {
      toggleEdit();
      return;
    }
    save();
  };

  let childNode = children;

  if (title === "Responders") {
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
            onBlur={onBlur}
            onPressEnter={save}
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

interface TableProps {
  data: DeviceTracker[] | undefined;
  onNameChange: (deviceUID: string, updatedName: string) => Promise<boolean>;
}

const TableComponent = ({ data, onNameChange }: TableProps) => {
  const editableColumns = columns.map((col) => ({
    ...col,
    onCell: (record: DeviceTracker) => ({
      record,
      title: col.title,
      onChange: onNameChange,
    }),
  }));

  return (
    <div className={styles.tableContainer}>
      <Table
        components={{
          body: {
            cell: EditableCell,
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

export default TableComponent;
