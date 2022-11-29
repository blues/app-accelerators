import { Switch, Table, Tag } from "antd";
import type { ColumnsType } from "antd/lib/table";
import { ValveMonitorDevice } from "../../services/AppModel";
import styles from "../../styles/ValveMonitorTable.module.scss";

const columns = [
  {
    title: "Valve Location",
    dataIndex: "name",
    key: "name",
  },
  {
    title: (
      <>
        <div>Flow Rate</div>
        <div> mL/min</div>
      </>
    ),
    dataIndex: "deviceFlowRate",
    key: "deviceFlowRate",
    align: "center",
  },
  {
    title: (
      <>
        <div>Monitoring</div>
        <div>Frequency (min)</div>
      </>
    ),
    dataIndex: "monitorFrequency",
    key: "monitorFrequency",
    align: "center",
  },
  {
    title: "Alarm Threshold",
    colSpan: 2,
    children: [
      {
        title: "Min",
        dataIndex: "minFlowThreshold",
        colSpan: 0,
        align: "center",
        onCell: () => ({ rowSpan: 1 }),
      },
      {
        title: "Max",
        dataIndex: "maxFlowThreshold",
        colSpan: 0,
        align: "center",
        onCell: () => ({ rowSpan: 1 }),
      },
    ],
    dataIndex: "deviceAlarmThreshold",
    key: "deviceAlarmThreshold",
    align: "center",
  },
  {
    title: "Alarm",
    dataIndex: "deviceAlarm",
    key: "deviceAlarm",
    align: "center",
  },
  {
    title: "Valve State",
    render: (_, record) => (
      <Tag color={record.valveState === "open" ? "success" : "warning"}>
        {record.valveState}
      </Tag>
    ),
    align: "center",
  },
  {
    title: "Valve Control (open/closed)",
    align: "center",
    width: "15%",
    render: (_, record) => <Switch checked={record.valveState === "open"} />,
  },
] as ColumnsType<ValveMonitorDevice>;

interface ValveMonitorTableProps {
  data: ValveMonitorDevice[] | undefined;
  // setIsErrored: (isErrored: boolean) => void;
  // setIsLoading: (isLoading: boolean) => void;
  // setErrorMessage: (errorMessage: string) => void;
  // refreshData: () => Promise<void>;
}

const ValveMonitorTable = ({
  data,
}: // refreshData,
// setIsErrored,
// setIsLoading,
// setErrorMessage,
ValveMonitorTableProps) => (
  <div className={styles.tableContainer}>
    <Table columns={columns} dataSource={data} pagination={false} />
  </div>
);
export default ValveMonitorTable;
