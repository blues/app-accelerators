import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { Alert, Col, Row } from "antd";
import { useEffect, useState } from "react";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import MonitorFrequencyCard from "../components/elements/MonitorFrequencyCard";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import styles from "../styles/Home.module.scss";
import { ValveMonitorConfig } from "../services/AppModel";
import AlarmThresholdCard from "../components/elements/AlarmThresholdCard";

type HomeData = {
  valveMonitorConfig: ValveMonitorConfig;
  err?: string;
};

const Home: NextPage<HomeData> = ({ valveMonitorConfig, err }) => {
  const infoMessage = "Deploy message";

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

  const router = useRouter();
  // refresh the page
  const refreshData = async () => {
    // eslint-disable-next-line no-void
    void router.replace(router.asPath);
  };

  useEffect(() => {
    refreshData();
  }, [monitorFrequency, minFlowThreshold, maxFlowThreshold]);

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
          <div>
            {isErrored && (
              <Alert type="error" message={errorMessage} closable />
            )}
            <h3 className={styles.sectionTitle}>Fleet Controls</h3>
            <Row gutter={16}>
              <Col className={styles.motionFrequencyCard}>
                <MonitorFrequencyCard
                  currentFrequency={monitorFrequency}
                  setCurrentFrequency={setMonitorFrequency}
                  setErrorMessage={setErrorMessage}
                  setIsErrored={setIsErrored}
                  setIsLoading={setIsLoading}
                />
              </Col>
              <Col className="">
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
