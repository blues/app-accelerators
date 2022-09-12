import { Table } from "antd";
import type { ColumnsType } from "antd/es/table";

// using object and any to make table more reusable / extensible
export interface TableProps {
  columns: ColumnsType<object>;
  data: any[];
}

const TableComponent = ({ columns, data }: TableProps) => (
  <>
    <p>Device Data Table</p>
    {data ? <Table rowKey="uid" columns={columns} dataSource={data} /> : null}
  </>
);
export default TableComponent;
