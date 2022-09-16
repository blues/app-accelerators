import { Table } from "antd";
import type { ColumnsType } from "antd/es/table";

// using object and any to make table more reusable / extensible
export interface TableProps {
  columns: ColumnsType<object>;
  data: object[] | undefined;
}

const TableComponent = ({ columns, data }: TableProps) => (
  <Table rowKey="uid" columns={columns} dataSource={data} pagination={false} />
);
export default TableComponent;
