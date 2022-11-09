import { GetServerSideProps, NextPage } from "next";
import { Alert } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { getCombinedDeviceEventsInfo } from "../components/presentation/deviceEventInfo";
import { ERROR_CODES } from "../services/Errors";
import DeviceCard from "../components/elements/DeviceCard";
import styles from "../styles/Home.module.scss";
import Config from "../../Config";

type HomeData = {
  deviceEventDataList: any[];
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
  // just to get something on screen
  let devices: any = [];
  let deviceEvents: any = [];
  let deviceEventDataList: any = [];

  try {
    const appService = services().getAppService();
    devices = await appService.getDevices();
    deviceEvents = await appService.getDeviceEvents(
      devices.map((device) => device.deviceUID)
    );

    // fetch fleets by project
    const fleetsForProject = await appService.getFleetsByProject();
    console.log("FLEETS----", fleetsForProject);

    // fetch devices associated with fleets
    const devicesByFleet = await appService.getDevicesByFleet(
      fleetsForProject.fleets.map((fleet) => fleet.uid)
    );
    console.log("DEVICES BY FLEET-------------", devicesByFleet);

    // save fleet uids to db
    // todo how do I reshape this data so handleEvent can use it?
    // const storeFleetInDB = await appService.handleEvent(
    //   devicesByFleet.devices.map((device) =>
    //     devicesByFleetFromNotehubApiEvent(device)
    //   )
    // );
    // console.log(storeFleetInDB);

    // fetch env vars associated with fleets

    // and save fleet env vars from Notehub and associate device with fleet

    // combine the devices with their events
    deviceEventDataList = getCombinedDeviceEventsInfo(devices, deviceEvents);

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
