import { useEffect, useState } from "react";
import { GetServerSideProps, NextPage } from "next";
import Image from "next/image";
import { Alert, Col, Row, Tooltip } from "antd";
import { useRouter } from "next/router";
import ResponderIcon from "../components/elements/images/responder.svg";
import { ERROR_CODES } from "../services/Errors";
import { DeviceTracker, TrackerConfig } from "../services/ClientModel";
import { ERROR_MESSAGE, getErrorMessage } from "../constants/ui";
import Config from "../../Config";
import Table, { TableProps } from "../components/elements/Table";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { services } from "../services/ServiceLocatorServer";
import { updateLiveTrackerStatus } from "../api-client/fleetVariables";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import LiveTrackCard from "../components/elements/LiveTrackCard";
import RespondersByFloorTable from "../components/elements/RespondersByFloorTable";
import styles from "../styles/Home.module.scss";

type HomeData = {
  fleetTrackerConfig: TrackerConfig;
  error: string;
};

const Home: NextPage<HomeData> = ({ fleetTrackerConfig, error }) => {
  const infoMessage = "Deploy message";
  const MS_REFETCH_INTERVAL = 10000;
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [isErred, setIsErred] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string>("");
  const [respondersGroupedByFloor, setRespondersGroupedByFloor] =
    useState<object>([]);

  const router = useRouter();
  // refresh the page
  const refreshData = async () => {
    await router.replace(router.asPath);
  };

  const {
    isLoading: deviceTrackersLoading,
    error: deviceTrackersError,
    data: deviceTrackers,
  } = useDeviceTrackerData(MS_REFETCH_INTERVAL);

  const err =
    deviceTrackersError && getErrorMessage(deviceTrackersError.message);

  const trackers: DeviceTracker[] | undefined = deviceTrackers;

  // todo clean up naming of func and vars and doesn't show table until loading is false
  const groupBy = (array: any[], prop: string) =>
    array.reduce((acc, obj) => {
      const key = obj[prop];
      if (key) {
        acc[key] ? ++acc[key] : (acc[key] = 1);
      }
      return acc;
    }, {});

  useEffect(() => {
    if (trackers) {
      const devicesByFloor = groupBy(trackers, "floor");
      console.log(devicesByFloor);
      const transformRawData = Object.entries(devicesByFloor).map(
        ([key, value]) => ({
          floorNumber: `Floor ${key}`,
          responderCount: `${value}`,
        })
      );
      console.log(transformRawData);
      setRespondersGroupedByFloor([...transformRawData]);
    }
  }, [trackers]);

  const liveTrackEnabled: boolean | undefined = !!fleetTrackerConfig?.live;

  const toggleLiveTracking = async (checked: boolean) => {
    setIsErred(false);
    setErrorMessage("");
    setIsLoading(true);
    let isSuccessful = true;
    try {
      await updateLiveTrackerStatus(checked);
    } catch (e) {
      setIsErred(true);
      setErrorMessage(ERROR_MESSAGE.UPDATE_FLEET_LIVE_STATUS_FAILED);
      isSuccessful = false;
    }
    setIsLoading(false);
    await refreshData();
    return isSuccessful;
  };

  const tableInfo: TableProps = {
    columns: [
      {
        title: (
          <>
            <Image src={ResponderIcon} alt="person outline" />
            <span style={{ marginLeft: "8px" }}>Responders</span>
          </>
        ),
        dataIndex: "name",
        key: "name",
        width: "20%",
        ellipsis: { showTitle: false },
        render: (name) => (
          <Tooltip placement="topLeft" title={name}>
            {name}
          </Tooltip>
        ),
      },
      {
        title: "Floor",
        dataIndex: "floor",
        key: "floor",
        width: "15%",
      },
      {
        title: "Last Seen",
        dataIndex: "lastActivity",
        key: "lastActivity",
        width: "30%",
      },
      {
        title: "Pressure",
        dataIndex: "pressure",
        key: "pressure",
        width: "20%",
      },
      {
        title: "Voltage",
        dataIndex: "voltage",
        key: "voltage",
        width: "15%",
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
          dangerouslySetInnerHTML={{ __html: err || error }}
        />
      ) : (
        <LoadingSpinner isLoading={isLoading || deviceTrackersLoading}>
          <div>
            {isErred && <Alert type="error" message={errorMessage} closable />}
            <h3 className={styles.sectionTitle}>Fleet Controls</h3>
            <Row>
              <Col span={3}>
                <LiveTrackCard
                  liveTrackEnabled={liveTrackEnabled}
                  toggleLiveTracking={toggleLiveTracking}
                />
              </Col>
            </Row>
            <Row>
              <h3 className={styles.sectionTitle}>Fleet Name</h3>
              {trackers && (
                <Table columns={tableInfo.columns} data={tableInfo.data} />
              )}
              {respondersGroupedByFloor && (
                <RespondersByFloorTable data={respondersGroupedByFloor} />
              )}
            </Row>
            {Config.isBuildVersionSet() ? (
              <Alert description={infoMessage} type="info" closable />
            ) : null}
          </div>
        </LoadingSpinner>
      )}
    </div>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  let fleetTrackerConfig: TrackerConfig = {};
  let error = "";

  try {
    const appService = services().getAppService();
    fleetTrackerConfig = await appService.getTrackerConfig();

    return {
      props: { fleetTrackerConfig, error },
    };
  } catch (e) {
    error = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { fleetTrackerConfig, error },
  };
};
