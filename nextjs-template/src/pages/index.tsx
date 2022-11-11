import { GetServerSideProps, NextPage } from "next";
import { Alert } from "antd";
import { uniq } from "lodash";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { getNormalizedDeviceData } from "../components/presentation/deviceEventInfo";
import { Device, Event } from "../services/DomainModel";
import { ERROR_CODES } from "../services/Errors";
import DeviceCard from "../components/elements/DeviceCard";
import styles from "../styles/Home.module.scss";
import Config from "../../Config";

type HomeData = {
  deviceEventDataList: any;
  err?: string;
};

const Home: NextPage<HomeData> = ({ deviceEventDataList, err }) => {
  const infoMessage = "Deploy message";

  return (
    <div className={styles.container}>
      {err ? (
        <h2
          className={styles.errorMessage}
          // life in the fast lane...
          // eslint-disable-next-line react/no-danger
          dangerouslySetInnerHTML={{ __html: err }}
        />
      ) : (
        <>
          {Config.isBuildVersionSet() ? (
            <Alert description={infoMessage} type="info" closable />
          ) : null}
          <h2>Devices</h2>
          {deviceEventDataList.map((deviceData, index) => (
            <DeviceCard key={deviceData.deviceID} deviceDetails={deviceData} />
          ))}
        </>
      )}
    </div>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  let err = "";

  let devices: Device[] = [];
  let deviceEvents: Event[] = [];
  let deviceEventDataList: any = [];

  try {
    const appService = services().getAppService();
    devices = await appService.getDevices();
    deviceEvents = await appService.getDeviceEvents(
      devices.map((device) => device.id.deviceUID)
    );

    // fetch all unique fleets associated with devices
    const uniqueFleets = uniq(devices.map((device) => device.fleetUIDs).flat());

    // fetch fleet env vars from Notehub
    const fleetEnvVars = await Promise.all(
      uniqueFleets.map((fleetUID) => appService.getFleetEnvVars(fleetUID))
    );

    // fetch device env vars from Notehub
    const deviceEnvVars = await Promise.all(
      devices.map((device) => appService.getDeviceEnvVars(device.id.deviceUID))
    );

    // combine the devices with their events and fleet and device level env vars
    deviceEventDataList = getNormalizedDeviceData(
      devices,
      deviceEvents,
      deviceEnvVars,
      fleetEnvVars
    );

    console.log(deviceEventDataList);

    return {
      props: { deviceEventDataList, err },
    };
  } catch (e) {
    err = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { deviceEventDataList, err },
  };
};
