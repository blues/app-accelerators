import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { Alert, Button, Col, Row } from "antd";
import { useEffect, useState } from "react";
import { useQueryClient } from "react-query";
import { setCookie, getCookie, CookieValueTypes } from "cookies-next";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import { AuthToken, DeviceTracker, TrackerConfig } from "../services/AppModel";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { services } from "../services/ServiceLocatorServer";
import { services as clientSideServices } from "../services/ServiceLocatorClient";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import LiveTrackCard from "../components/elements/LiveTrackCard";
import RespondersByFloorTable from "../components/elements/RespondersByFloorTable";
import TrackerTable from "../components/elements/TrackerTable";
import MotionAlertConfigCard from "../components/elements/MotionAlertConfigCard";
import styles from "../styles/Home.module.scss";

type HomeData = {
  fleetTrackerConfig: TrackerConfig;
  error: string;
};

const Home: NextPage<HomeData> = ({ fleetTrackerConfig, error }) => {
  // How often to refetch data from Notehub (in milliseconds). Note that
  // reading data with the Notehub API uses one consumption credit per event
  // (see https://blues.io/pricing/).
  const MS_REFETCH_INTERVAL = 60 * 1000;

  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [isErrored, setIsErrored] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string>("");
  const [isLiveTrackingEnabled, setIsLiveTrackingEnabled] =
    useState<boolean>(false);
  const [currentNoMovementValue, setCurrentNoMovementValue] =
    useState<number>(120);

  const router = useRouter();
  const queryClient = useQueryClient();
  const alarmService = clientSideServices().getAlarmService();
  // refresh the page
  const refreshData = async () => {
    // eslint-disable-next-line no-void
    void router.replace(router.asPath);
  };
  const refreshDataAndInvalidateCache = async () => {
    // Clear the client-side cache so when we refresh the page
    // it refetches data to get the updated name.
    await queryClient.invalidateQueries();
    await refreshData();
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
    if (fleetTrackerConfig && fleetTrackerConfig.noMovementThreshold) {
      setCurrentNoMovementValue(fleetTrackerConfig.noMovementThreshold);
    }
  }, [fleetTrackerConfig]);

  const clearAlarms = async () => {
    alarmService.setLastAlarmClear();
    await refreshData();
  };

  useEffect(() => {
    refreshData();
  }, [isLiveTrackingEnabled, currentNoMovementValue]);

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
                <Row gutter={16}>
                  <Col className={styles.motionAlertCard}>
                    <MotionAlertConfigCard
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      currentNoMovementThreshold={currentNoMovementValue}
                      setCurrentNoMovementThreshold={setCurrentNoMovementValue}
                    />
                  </Col>
                  <Col className={styles.liveTrackCard}>
                    <LiveTrackCard
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      isLiveTrackingEnabled={isLiveTrackingEnabled}
                      setIsLiveTrackingEnabled={setIsLiveTrackingEnabled}
                    />
                  </Col>
                </Row>
                <Row gutter={[16, 24]}>
                  <Col xs={24} sm={24} md={24} lg={20}>
                    <div className={styles.tableHeaderRow}>
                      <h3 className={styles.sectionTitle}>Fleet</h3>
                      {alarmService.areAlarmsPresent(trackers) && (
                        <Button
                          type="primary"
                          danger
                          onClick={clearAlarms}
                          size="large"
                        >
                          Clear Alarms
                        </Button>
                      )}
                    </div>
                  </Col>
                  <Col xs={12} sm={7} md={6} lg={4} />
                </Row>
                <Row gutter={[16, 24]}>
                  <Col xs={24} sm={24} md={24} lg={20}>
                    <TrackerTable
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      refreshData={refreshDataAndInvalidateCache}
                      data={trackers}
                    />
                  </Col>
                  <Col xs={12} sm={7} md={6} lg={4}>
                    <RespondersByFloorTable data={trackers} />
                  </Col>
                </Row>
              </>
            )}
          </div>
        </LoadingSpinner>
      )}
    </div>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async ({
  req,
  res,
}) => {
  let fleetTrackerConfig: TrackerConfig = {};
  let authStringObj: CookieValueTypes;
  let error = "";

  try {
    const appService = services().getAppService();

    authStringObj = getCookie("authTokenObj", { req, res });
    let authObj: AuthToken = {};
    if (authStringObj === undefined) {
      authObj = await appService.getAuthToken();
      authStringObj = JSON.stringify(authObj);
    }
    const isAuthTokenValid = appService.checkAuthTokenValidity(authStringObj);
    if (!isAuthTokenValid) {
      authObj = await appService.getAuthToken();
      authStringObj = JSON.stringify(authObj);
    }
    setCookie("authTokenObj", authStringObj, {
      req,
      res,
    });

    fleetTrackerConfig = await appService.getTrackerConfig(authObj);

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
