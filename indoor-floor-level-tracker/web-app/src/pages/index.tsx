import { useState } from "react";
import { GetServerSideProps, NextPage } from "next";
import Image from "next/image";
import { Alert, Col, Row, Tooltip } from "antd";
import { useRouter } from "next/router";
import ResponderIcon from "../components/elements/images/responder.svg";
import { ERROR_CODES } from "../services/Errors";
import { DeviceTracker, TrackerConfig } from "../services/ClientModel";
import { getErrorMessage } from "../constants/ui";
import Config from "../../Config";
import Table, { TableProps } from "../components/elements/Table";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { services } from "../services/ServiceLocatorServer";
import { updateLiveTrackerStatus } from "../api-client/fleetVariables";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import LiveTrackCard from "../components/elements/LiveTrackCard";
import styles from "../styles/Home.module.scss";

type HomeData = {
  fleetTrackerConfig: TrackerConfig;
  error: string;
};

const Home: NextPage<HomeData> = ({ fleetTrackerConfig, error }) => {
  const infoMessage = "Deploy message";
  const MS_REFETCH_INTERVAL = 10000;
  const [isLoading, setIsLoading] = useState<boolean>(false);

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
  console.log(JSON.stringify(trackers));

  const groupBy = (array: array, prop: string) =>
    array.reduce((acc, obj) => {
      const key = obj[prop];
      if (!acc[key]) {
        acc[key] = 1;
      }
      acc[key] + 1;
      return acc;
    }, {});

  const respondersGroupedByFloor = groupBy(deviceTrackers, "floor");
  console.log(respondersGroupedByFloor);

  const liveTrackEnabled: boolean | undefined = !!fleetTrackerConfig?.live;

  const toggleLiveTracking = async (checked: boolean) => {
    let isSuccessful = true;
    setIsLoading(true);
    try {
      await updateLiveTrackerStatus(checked);
    } catch (e) {
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

  const respondersTableInfo: TableProps = {
    columns: [
      {
        title: "Responders Per Floor",
        dataIndex: "floorNumber",
        key: "floorNumber",
      },
    ],
    data: null,
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
