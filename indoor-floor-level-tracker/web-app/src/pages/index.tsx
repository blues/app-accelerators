import { useEffect, useState, useRef } from "react";
import { GetServerSideProps, NextPage } from "next";
import { Alert, Table } from "antd";
import type { ColumnsType } from "antd/es/table";
import { format } from "date-fns";
import { usePubNub } from "pubnub-react";
import { uniqBy } from "lodash";
import { services } from "../services/ServiceLocatorServer";
import { ERROR_MESSAGE, getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import styles from "../styles/Home.module.scss";

type HomeData = {
  err?: string;
};

// Notehub data.qo properties
interface DataType {
  bestID: string;
  deviceID: string;
  latitude: number;
  longitude: number;
  location: string;
  timestamp: number;
  altitude: number;
  floor: number;
  pressure: number;
  temperature: number;
}

const Home: NextPage<HomeData> = ({ err }) => {
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

  const columns: ColumnsType<DataType> = [
    {
      title: "Best ID",
      dataIndex: "best_id",
      key: "bestID",
    },
    {
      title: "Device ID",
      dataIndex: "device",
      key: "deviceID",
    },
    {
      title: "Latitude",
      dataIndex: "best_lat",
      key: "latitude",
    },
    {
      title: "Longitude",
      dataIndex: "best_lon",
      key: "longitude",
    },
    {
      title: "Location",
      dataIndex: "best_location",
      key: "location",
    },
    {
      title: "Timestamp",
      dataIndex: "when",
      key: "timestamp",
      render: (time) => (
        <p>{format(new Date(time * 1000), "MM/dd HH:mm:ss a")}</p>
      ),
    },
    {
      title: "Altitude",
      dataIndex: ["body", "altitude"],
      key: "altitude",
    },
    {
      title: "Floor",
      dataIndex: ["body", "floor"],
      key: "floor",
    },
    {
      title: "Pressure",
      dataIndex: ["body", "pressure"],
      key: "pressure",
    },
    {
      title: "Temperature (C)",
      dataIndex: ["body", "temp"],
      key: "temperature",
    },
  ];

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
          <p>Device Data Table</p>
          {data ? (
            <Table
              rowKey={(data) => data.deviceID}
              columns={columns}
              dataSource={data}
            />
          ) : null}
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
  let err = "";

  try {
    const appService = services().getAppService();

    return {
      props: { err },
    };
  } catch (e) {
    err = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { err },
  };
};
