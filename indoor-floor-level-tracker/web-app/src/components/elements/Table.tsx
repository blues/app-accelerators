import { Table } from "antd";
import type { ColumnsType } from "antd/es/table";
import { DeviceTracker } from "../../services/ClientModel";

type TableData = {
  data: DeviceTracker[];
};

const TableComponent = ({ data }: TableData) => {
  const columns: ColumnsType<DeviceTracker> = [
    {
      title: "Device Name",
      dataIndex: "name",
      key: "name",
    },
    {
      title: "Device ID",
      dataIndex: "uid",
      key: "uid",
    },
    {
      title: "Location",
      dataIndex: "location",
      key: "location",
    },
    {
      title: "Timestamp",
      dataIndex: "lastActivity",
      key: "lastActivity",
    },
    {
      title: "Altitude",
      dataIndex: "altitude",
      key: "altitude",
    },
    {
      title: "Floor",
      dataIndex: "floor",
      key: "floor",
    },
    {
      title: "Pressure",
      dataIndex: "pressure",
      key: "pressure",
    },
    {
      title: "Temperature",
      dataIndex: "temp",
      key: "temperature",
    },
  ];

  return (
    <>
      <p>Device Data Table</p>
      {data ? <Table rowKey="uid" columns={columns} dataSource={data} /> : null}
    </>
  );
};

export default TableComponent;
