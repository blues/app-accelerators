import { useEffect, useState, useRef } from "react";
import { GetServerSideProps, NextPage } from "next";
import { Alert } from "antd";
import { usePubNub } from "pubnub-react";
import { uniqBy } from "lodash";
import { DeviceTracker } from "../services/ClientModel";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import Table from "../components/elements/Table";
import styles from "../styles/Home.module.scss";

type HomeData = {
  deviceTrackers: DeviceTracker[];
  err?: string;
};

const Home: NextPage<HomeData> = ({ deviceTrackers, err }) => {
  const infoMessage = "Deploy message";
  let pubnub;
  if (pubnub) {
    pubnub = usePubNub();
  }
  const [channels] = useState(["nf1-test"]); // todo make this a constant?
  const dataRef = useRef(); // holds previous device state when page re-renders from PubNub update
  const [data, setData] = useState([]);

  const fetchPubNubHistory = () => {
    pubnub.fetchMessages(
      {
        channels: ["nf1-test"],
      },
      (status, response) => {
        console.log(status, response);
        const sortedEvents = response.channels["nf1-test"].sort(
          (a, b) => parseFloat(b.message.when) - parseFloat(a.message.when)
        );
        const uniqueDevices = uniqBy(sortedEvents, "message.device");
        const devicesData = uniqueDevices.map((device) => device.message);

        dataRef.current = devicesData; // ref needed to hold values after page re-renders from state update
        setData(devicesData);
      }
    );
  };

  const handleEvent = (event) => {
    let msgArr: DataType[] = [];

    const updatedArr = dataRef.current.map((device) => {
      if (event.message.device === device.device) {
        return event.message;
      }
      return device;
    });

    msgArr = updatedArr;
    dataRef.current = msgArr; // update ref before next event comes through and this function's called again
    setData(msgArr);
  };

  useEffect(() => {
    // if pubnub is enabled, subscribe to listeners and fetch latest device history data
    if (pubnub) {
      pubnub.addListener({ message: handleEvent });
      pubnub.subscribe({ channels });

      fetchPubNubHistory();
    }
  }, [pubnub, channels]);

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
          <Table data={deviceTrackers} />
          {Config.isBuildVersionSet() ? (
            <Alert description={infoMessage} type="info" closable />
          ) : null}
        </>
      )}
    </div>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  let deviceTrackers: DeviceTracker[] = [];
  let err = "";

  try {
    const appService = services().getAppService();
    // fetch device tracker data
    deviceTrackers = await appService.getDeviceTrackerData();

    return {
      props: { deviceTrackers, err },
    };
  } catch (e) {
    err = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { deviceTrackers, err },
  };
};
