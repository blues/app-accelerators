import React, { useEffect, useRef, useState } from "react";
import { Form, Input, InputRef, Table } from "antd";
import { DeviceTracker } from "../../services/ClientModel";
import styles from "../../styles/Table.module.scss";

const columns = [
  {
    title: "Responders",
    dataIndex: "name",
    key: "name",
    editable: true,
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
];

interface EditableCellProps {
  editable: boolean;
  children: JSX.Element;
  record: DeviceTracker;
  onChange: (deviceUID: string, updatedName: string) => Promise<boolean>;
}

const EditableCell = ({
  editable,
  children,
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
    const newName = form.getFieldValue("name");
    if (newName.trim() === "") {
      return;
    }

    if (record.name === newName) {
      toggleEdit();
      return;
    }

    onChange(record.uid, newName)
      .then(() => {
        toggleEdit();
      })
      .catch(() => {
        toggleEdit();
      });
  };

  let childNode = children;

  if (editable) {
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
            onBlur={save}
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

const tableComponents = {
  body: {
    cell: EditableCell,
  },
};

interface TableProps {
  data: DeviceTracker[] | undefined;
  onNameChange: (deviceUID: string, updatedName: string) => Promise<boolean>;
}

const TableComponent = ({ data, onNameChange }: TableProps) => {
  const editableColumns = columns.map((col) => {
    console.log(col.key);
    if (!col.editable) {
      return col;
    }

    return {
      ...col,
      onCell: (record: DeviceTracker) => ({
        record,
        editable: col.editable,
        onChange: onNameChange,
      }),
    };
  });

  return (
    <div className={styles.tableContainer}>
      <Table
        components={tableComponents}
        rowKey="uid"
        columns={editableColumns}
        dataSource={data}
        pagination={false}
      />
    </div>
  );
};

export default TableComponent;
