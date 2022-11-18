import { Table } from "antd";
import type { ColumnsType } from "antd/lib/table";
import { ValveMonitorDevice } from "../../services/AppModel";

const columns = [
  {
    title: "Valve Location",
    dataIndex: "name",
    key: "name",
  },
  {
    title: "Flow Rate mL/min",
    dataIndex: "deviceFlowRate",
    key: "deviceFlowRate",
  },
  {
    title: "Monitoring Frequency (min)",
    dataIndex: "deviceFlowRateFrequency",
    key: "deviceFlowRateFrequency",
  },
  {
    title: "Alarm Threshold (+/- mL)",
    dataIndex: "deviceAlarmThreshold",
    key: "deviceAlarmThreshold",
  },
  {
    title: "Alarm",
    dataIndex: "deviceAlarm",
    key: "deviceAlarm",
  },
  {
    title: "Valve State",
    dataIndex: "valveState",
    key: "valveState",
  },
  {
    title: "Valve Control (open/closed)",
    dataIndex: "",
    key: "",
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
ValveMonitorTableProps) => {
  console.log(data);
  return (
    <div>
      <Table
        columns={columns}
        dataSource={data.valveMonitorDevices}
        pagination={false}
      />
    </div>
  );
};

export default ValveMonitorTable;
