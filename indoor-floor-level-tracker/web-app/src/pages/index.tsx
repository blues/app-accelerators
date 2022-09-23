import { useEffect, useState } from "react";
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
  const [isErrored, setIsErrored] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string>("");
  const [isLiveTrackingEnabled, setIsLiveTrackingEnabled] =
    useState<boolean>(false);

  const router = useRouter();
  // refresh the page
  const refreshData = () => {
    // eslint-disable-next-line no-void
    void router.replace(router.asPath);
  };

  const { error: deviceTrackersError, data: deviceTrackers } =
    useDeviceTrackerData(MS_REFETCH_INTERVAL);

  const err =
    deviceTrackersError && getErrorMessage(deviceTrackersError.message);

  const trackers: DeviceTracker[] | undefined = deviceTrackers;

  useEffect(() => {
    if (fleetTrackerConfig && fleetTrackerConfig.live) {
      setIsLiveTrackingEnabled(fleetTrackerConfig.live);
    }
  }, [fleetTrackerConfig]);

  useEffect(() => {
    refreshData();
  }, [isLiveTrackingEnabled]);

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
        <LoadingSpinner isLoading={isLoading}>
          <div>
            {isErrored && (
              <Alert type="error" message={errorMessage} closable />
            )}
            {trackers && (
              <>
                <h3 className={styles.sectionTitle}>Fleet Controls</h3>
                <Row>
                  <Col span={3}>
                    <LiveTrackCard
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      isLiveTrackingEnabled={isLiveTrackingEnabled}
                      setIsLiveTrackingEnabled={setIsLiveTrackingEnabled}
                    />
                  </Col>
                </Row>
                <h3 className={styles.sectionTitle}>Fleet Name</h3>
                <Row gutter={16}>
                  <Col span={19}>
                    <Table columns={tableInfo.columns} data={tableInfo.data} />
                  </Col>
                  <Col span={4}>
                    <RespondersByFloorTable data={trackers} />
                  </Col>
                </Row>
              </>
            )}
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
