import { Table } from "antd";
import type { ColumnsType } from "antd/lib/table";
import styles from "../../styles/Table.module.scss";

interface DataType {
  floorNumber: string;
  responderCount: string;
}

const columns = [
  {
    title: "Responders Per Floor",
    colSpan: 2,
    children: [
      {
        title: "Floor Number",
        dataIndex: "floorNumber",
        colSpan: 0,
        onCell: () => ({ rowSpan: 1 }),
      },
      // todo style up numbers somewhere (probably in index.tsx file)
      {
        title: "Responder Count",
        dataIndex: "responderCount",
        colSpan: 0,
        onCell: () => ({ rowSpan: 1 }),
      },
    ],
  },
] as ColumnsType<DataType>;

const RespondersByFloorTableComponent = ({ data }) => (
  <div className={styles.tableContainer}>
    <Table
      columns={columns}
      rowKey="floorNumber"
      dataSource={data}
      pagination={false}
    />
  </div>
);

export default RespondersByFloorTableComponent;
