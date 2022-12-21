import { useEffect, useState } from "react";
import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { useQueryClient } from "react-query";
import { Alert, Button, Col, Row } from "antd";
import { clearAlarms } from "../api-client/alarms";
import { useValveMonitorDeviceData } from "../hooks/useValveMonitorDeviceData";
import AlarmThresholdCard from "../components/elements/AlarmThresholdCard";
import MonitorFrequencyCard from "../components/elements/MonitorFrequencyCard";
import ValveMonitorTable from "../components/elements/ValveMonitorTable";
import LoadingSpinner from "../components/layout/LoadingSpinner";
import { getErrorMessage, ERROR_MESSAGE } from "../constants/ui";
import { ValveMonitorConfig } from "../services/AppModel";
import { services } from "../services/ServiceLocatorServer";
import { ERROR_CODES } from "../services/Errors";
import styles from "../styles/Home.module.scss";

type HomeData = {
  valveMonitorConfig: ValveMonitorConfig;
  err?: string;
};

const Home: NextPage<HomeData> = ({ valveMonitorConfig, err }) => {
  // How often to refresh that page’s data (in milliseconds)
  const MS_REFETCH_INTERVAL = 60 * 1000;

  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [isErrored, setIsErrored] = useState<boolean>(false);
  const [isClearingAlarms, setIsClearingAlarms] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string>("");
  const [monitorFrequency, setMonitorFrequency] = useState<number>(
    valveMonitorConfig.monitorFrequency
  );
  const [minFlowThreshold, setMinFlowThreshold] = useState<number>(
    valveMonitorConfig.minFlowThreshold
  );
  const [maxFlowThreshold, setMaxFlowThreshold] = useState<number>(
    valveMonitorConfig.maxFlowThreshold
  );

  const { error: valveMonitorDevicesError, data: valveMonitorDeviceList } =
    useValveMonitorDeviceData(MS_REFETCH_INTERVAL);
  const error =
    valveMonitorDevicesError &&
    getErrorMessage(valveMonitorDevicesError.message);

  const valveMonitorDevices = valveMonitorDeviceList;

  const router = useRouter();
  const queryClient = useQueryClient();

  // refresh the page
  const refreshDataAndInvalidateCache = async () => {
    // Clear the client-side cache so when we refresh the page
    // it refetches data to get the updated table data.
    void router.replace(router.asPath);
    await queryClient.invalidateQueries();
  };

  const clearAllAlarms = async () => {
    setIsClearingAlarms(true);
    try {
      await clearAlarms();
    } catch (e) {
      setIsClearingAlarms(false);
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.CLEAR_ALARMS_FAILED);
    }
    await refreshDataAndInvalidateCache();
    setIsClearingAlarms(false);
  };

  useEffect(() => {
    refreshDataAndInvalidateCache();
  }, [monitorFrequency, minFlowThreshold, maxFlowThreshold]);

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
            <div>
              {valveMonitorDevices && (
                <>
                  <Row gutter={[16, 24]}>
                    <Col span={24}>
                      <div className={styles.tableHeaderRow}>
                        <h3 className={styles.sectionTitle}>Fleet Controls</h3>
                      </div>
                    </Col>
                  </Row>
                  <Row gutter={16}>
                    {/* relying on Ant D's responsive design column system: https://ant.design/components/grid#components-grid-demo-responsive */}
                    <Col
                      xs={11}
                      sm={8}
                      md={6}
                      lg={4}
                      xl={4}
                      className={styles.motionFrequencyCard}
                    >
                      <MonitorFrequencyCard
                        currentFrequency={monitorFrequency}
                        setCurrentFrequency={setMonitorFrequency}
                        setErrorMessage={setErrorMessage}
                        setIsErrored={setIsErrored}
                        setIsLoading={setIsLoading}
                      />
                    </Col>
                    <Col xs={13} sm={10} md={8} lg={6} xl={6}>
                      <AlarmThresholdCard
                        currentMinFlowThreshold={minFlowThreshold}
                        currentMaxFlowThreshold={maxFlowThreshold}
                        setCurrentMinFlowThreshold={setMinFlowThreshold}
                        setCurrentMaxFlowThreshold={setMaxFlowThreshold}
                        setIsErrored={setIsErrored}
                        setIsLoading={setIsLoading}
                        setErrorMessage={setErrorMessage}
                      />
                    </Col>
                  </Row>
                  <Row gutter={[16, 24]}>
                    <Col span={24}>
                      <div className={styles.tableHeaderRow}>
                        <h3 className={styles.sectionTitle}>
                          Individual Controls
                        </h3>
                        {valveMonitorDevices.filter(
                          (device) => device.deviceAlarm
                        ).length > 0 && (
                          <Button
                            type="primary"
                            danger
                            loading={isClearingAlarms}
                            onClick={clearAllAlarms}
                          >
                            Clear Alarms
                          </Button>
                        )}
                      </div>
                    </Col>
                  </Row>
                  <Row gutter={[16, 24]}>
                    <Col span={24}>
                      <ValveMonitorTable
                        data={valveMonitorDevices}
                        setIsErrored={setIsErrored}
                        setIsLoading={setIsLoading}
                        setErrorMessage={setErrorMessage}
                        refreshData={refreshDataAndInvalidateCache}
                      />
                    </Col>
                  </Row>
                </>
              )}
            </div>
          </div>
        </LoadingSpinner>
      )}
    </div>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  let valveMonitorConfig: ValveMonitorConfig = {};
  let err = "";

  try {
    const appService = services().getAppService();
    valveMonitorConfig = await appService.getValveMonitorConfig();

    return {
      props: { valveMonitorConfig, err },
    };
  } catch (e) {
    err = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { valveMonitorConfig, err },
  };
};
