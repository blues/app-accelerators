import { Table } from "antd";
import type { ColumnsType } from "antd/es/table";

// using object and any to make table more reusable / extensible
export interface TableProps {
  columns: ColumnsType<object>;
  data: any;
}

const TableComponent = ({ columns, data }: TableProps) => (
  <>
    {data ? (
      <Table
        rowKey="uid"
        columns={columns}
        dataSource={data}
        pagination={false}
      />
    ) : null}
  </>
);
export default TableComponent;
