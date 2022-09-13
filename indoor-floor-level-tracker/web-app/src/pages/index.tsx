import { useEffect, useState, useRef } from "react";
import { GetServerSideProps, NextPage } from "next";
import { Alert } from "antd";
import { usePubNub } from "pubnub-react";
import Pubnub, { FetchMessagesResponse } from "pubnub";
import { uniqBy } from "lodash";
import { DeviceTracker } from "../services/ClientModel";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import Table, { TableProps } from "../components/elements/Table";
import styles from "../styles/Home.module.scss";
import { useDeviceTrackerData } from "../api-client/devices";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";

// type HomeData = {
//   deviceTrackers: DeviceTracker[];
//   err?: string;
// };

const Home: NextPage = () => {
  const infoMessage = "Deploy message";
  const [isLoading, setIsLoading] = useState(false);
  const [trackers, setTrackers] = useState<DeviceTracker[]>();
  const [err, setErr] = useState<string | undefined>(undefined);
  const refetchInterval = 5000;

  const {
    isLoading: deviceTrackersLoading,
    error: deviceTrackersError,
    data: deviceTrackers,
    refetch: deviceTrackersRefetch,
  } = useDeviceTrackerData(refetchInterval);

  useEffect(() => {
    if (deviceTrackersError) {
      setErr(
        getErrorMessage(
          e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
        )
      );
    }
  }, [deviceTrackersError]);

  useEffect(() => {
    if (deviceTrackersLoading) {
      setIsLoading(true);
    }
  }, [deviceTrackersLoading]);

  useEffect(() => {
    console.log("Client side device trackers", deviceTrackers);
    if (deviceTrackers) {
      setTrackers(deviceTrackers);
    }
  }, [deviceTrackers]);

  // let pubnub: any;
  // if (pubnub) {
  //   pubnub = usePubNub();
  // }
  // const [channels] = useState(["nf1-test"]); // todo make this a constant?
  // const dataRef = useRef(); // holds previous device state when page re-renders from PubNub update
  // const [data, setData] = useState([]);

  // const fetchPubNubHistory = () => {
  //   pubnub.fetchMessages(
  //     {
  //       channels: ["nf1-test"],
  //     },
  //     (status: Pubnub.PubnubStatus, response: FetchMessagesResponse) => {
  //       console.log(status, response);
  //       const sortedEvents = response.channels["nf1-test"].sort(
  //         (a, b) => parseFloat(b.message.when) - parseFloat(a.message.when)
  //       );
  //       const uniqueDevices = uniqBy(sortedEvents, "message.device");
  //       const devicesData = uniqueDevices.map((device) => device.message);
  //       dataRef.current = devicesData; // ref needed to hold values after page re-renders from state update
  //       setData(devicesData);
  //     }
  //   );
  // };

  // const handleEvent = (event) => {
  //   let msgArr: DataType[] = [];

  //   const updatedArr = dataRef.current.map((device) => {
  //     if (event.message.device === device.device) {
  //       return event.message;
  //     }
  //     return device;
  //   });

  //   msgArr = updatedArr;
  //   dataRef.current = msgArr; // update ref before next event comes through and this function's called again
  //   setData(msgArr);
  // };

  // useEffect(() => {
  //   // if pubnub is enabled, subscribe to listeners and fetch latest device history data
  //   if (pubnub) {
  //     pubnub.addListener({ message: handleEvent });
  //     pubnub.subscribe({ channels });

  //     fetchPubNubHistory();
  //   }
  // }, [pubnub, channels]);

  const tableInfo: TableProps = {
    columns: [
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
    ],
    data: trackers,
  };

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
        <LoadingSpinner isLoading={isLoading}>
          {trackers && (
            <Table columns={tableInfo.columns} data={tableInfo.data} />
          )}
          {Config.isBuildVersionSet() ? (
            <Alert description={infoMessage} type="info" closable />
          ) : null}
        </LoadingSpinner>
      )}
    </div>
  );
};
export default Home;

// export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
//   let deviceTrackers: DeviceTracker[] = [];
//   let err = "";

//   try {
//     const appService = services().getAppService();
//     // fetch device tracker data
//     deviceTrackers = await appService.getDeviceTrackerData();

//     return {
//       props: { deviceTrackers, err },
//     };
//   } catch (e) {
//     err = getErrorMessage(
//       e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
//     );
//   }

//   return {
//     props: { deviceTrackers, err },
//   };
// };
