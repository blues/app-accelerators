/* eslint-disable @typescript-eslint/no-unsafe-return */
import { Table } from "antd";
import type { ColumnsType } from "antd/lib/table";
import { DeviceTracker } from "../../services/ClientModel";
import styles from "../../styles/RespondersByFloorTable.module.scss";

interface RespondersByFloorTableProps {
  data: DeviceTracker[];
}

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
      {
        title: "Responder Count",
        dataIndex: "responderCount",
        colSpan: 0,
        onCell: () => ({ rowSpan: 1 }),
        render: (_, record) => (
          <span className={styles.number}>{record.responderCount}</span>
        ),
      },
    ],
  },
] as ColumnsType<DataType>;

const RespondersByFloorTableComponent = ({
  data,
}: RespondersByFloorTableProps) => {
  const groupBy = (array: any[], prop: string) =>
    array.reduce((acc, obj) => {
      const key = obj[prop];
      if (key) {
        // eslint-disable-next-line no-plusplus
        acc[key] ? ++acc[key] : (acc[key] = 1);
      }
      return acc;
    }, {});

  const groupTrackerDevicesByFloor: Record<string, string | number> = groupBy(
    data,
    "floor"
  );
  const formatDeviceGroups = Object.entries(groupTrackerDevicesByFloor).map(
    ([key, value]) => ({
      floorNumber: `Floor ${key}`,
      responderCount: `${value}`,
    })
  );

  return (
    <div className={styles.respondersTableContainer}>
      <Table
        columns={columns}
        rowKey="floorNumber"
        dataSource={formatDeviceGroups}
        pagination={false}
      />
    </div>
  );
};

export default RespondersByFloorTableComponent;
