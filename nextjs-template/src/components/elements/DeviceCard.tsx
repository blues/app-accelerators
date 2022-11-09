import { Card } from "antd";
import { uniqBy } from "lodash";
import LineChart from "../charts/LineChart";

interface DeviceCardProps {
  deviceDetails: any;
  index: number;
}

const DeviceCardComponent = (props: DeviceCardProps) => {
  const { deviceDetails, index } = props;
  // some helper function here that transforms this raw data into the format required by the component's HTML
  // format the eventlist data for chart display (probably done by same function mentioned above)
  const getFormattedChartData = (eventList: [], reading: string) => {
    if (eventList.length) {
      const formattedData = uniqBy(
        eventList
          .filter((r) => r.eventName === "floor.qo")
          .map((filteredEvent) => {
            const chartDataObj = {
              when: filteredEvent.when,
              value: Number(filteredEvent.value[reading]),
            };
            return chartDataObj;
          }),
        "when"
      );
      return formattedData;
    }
    return [];
  };

  const tempChartData = getFormattedChartData(deviceDetails.eventList, "temp");
  const floorChartData = getFormattedChartData(
    deviceDetails.eventList,
    "floor"
  );
  const pressureChartData = getFormattedChartData(
    deviceDetails.eventList,
    "pressure"
  );
  const altitudeChartData = getFormattedChartData(
    deviceDetails.eventList,
    "altitude"
  );

  return (
    <Card title={deviceDetails.deviceID}>
      <h2>Current Device Details</h2>
      <div>
        Last Updated{` `}
        {deviceDetails.eventList[0].when}
      </div>
      <div>
        Current Floor{` `}
        {deviceDetails.eventList[0].value.floor}
      </div>
      <div>
        Temperature{` `}
        {deviceDetails.eventList[0].value.temp}
      </div>
      <div>
        Pressure{` `}
        {deviceDetails.eventList[0].value.pressure}
      </div>
      <div>
        Altitude{` `}
        {deviceDetails.eventList[0].value.altitude}
      </div>
      <h2>Historical Device Data</h2>
      <Card>
        <h3>Floor</h3>
        <LineChart label="Floor" data={floorChartData} chartColor="#9ccc65" />
      </Card>
      <Card>
        <h3>Temperature</h3>
        <LineChart
          label="Temperature"
          data={tempChartData}
          chartColor="#ba68c8"
        />
      </Card>
      <Card>
        <h3>Pressure</h3>
        <LineChart
          label="Pressure"
          data={pressureChartData}
          chartColor="#59d2ff"
        />
      </Card>
      <Card>
        <h3>Altitude</h3>
        <LineChart
          label="Altitude"
          data={altitudeChartData}
          chartColor="#ffd54f"
        />
      </Card>
    </Card>
  );
};

export default DeviceCardComponent;
