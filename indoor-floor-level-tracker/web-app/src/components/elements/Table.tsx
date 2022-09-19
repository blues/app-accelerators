import { Table } from "antd";
import type { ColumnsType } from "antd/es/table";
import styles from "../../styles/Table.module.scss";

// using object and any to make table more reusable / extensible
export interface TableProps {
  columns: ColumnsType<object>;
  data: object[] | undefined;
}

const TableComponent = ({ columns, data }: TableProps) => (
  <div className={styles.tableContainer}>
    <Table
      rowKey="uid"
      columns={columns}
      dataSource={data}
      pagination={false}
    />
  </div>
);
export default TableComponent;
