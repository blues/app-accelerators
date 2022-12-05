import { useEffect, useState } from "react";
import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { useQueryClient } from "react-query";
import { Alert, Col, Row } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { useValveMonitorDeviceData } from "../hooks/useValveMonitorDeviceData";
import AlarmThresholdCard from "../components/elements/AlarmThresholdCard";
import MonitorFrequencyCard from "../components/elements/MonitorFrequencyCard";
import ValveMonitorTable from "../components/elements/ValveMonitorTable";
import LoadingSpinner from "../components/layout/LoadingSpinner";
import { getErrorMessage } from "../constants/ui";
import { ValveMonitorConfig } from "../services/AppModel";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import styles from "../styles/Home.module.scss";

type HomeData = {
  valveMonitorConfig: ValveMonitorConfig;
  err?: string;
};

const Home: NextPage<HomeData> = ({ valveMonitorConfig, err }) => {
  const infoMessage = "Deploy message";
  const MS_REFETCH_INTERVAL = 60000;

  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [isErrored, setIsErrored] = useState<boolean>(false);
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
  const refreshData = async () => {
    // eslint-disable-next-line no-void
    void router.replace(router.asPath);
  };

  const refreshDataAndInvalidateCache = async () => {
    // Clear the client-side cache so when we refresh the page
    // it refetches data to get the updated table data.
    await refreshData();
    await queryClient.invalidateQueries();
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
                  <h3 className={styles.sectionTitle}>Fleet Controls</h3>
                  <Row gutter={16}>
                    <Col lg={4} xl={4} className={styles.motionFrequencyCard}>
                      <MonitorFrequencyCard
                        currentFrequency={monitorFrequency}
                        setCurrentFrequency={setMonitorFrequency}
                        setErrorMessage={setErrorMessage}
                        setIsErrored={setIsErrored}
                        setIsLoading={setIsLoading}
                      />
                    </Col>
                    <Col lg={7} xl={7}>
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
