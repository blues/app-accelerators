import { GetServerSideProps, NextPage } from "next";
import { Alert, Col, Row } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import MonitorFrequencyCard from "../components/elements/MonitorFrequencyCard";
import styles from "../styles/Home.module.scss";
import { ValveMonitorConfig } from "../services/AppModel";

type HomeData = {
  valveMonitorConfig: ValveMonitorConfig;
  err?: string;
};

const Home: NextPage<HomeData> = ({ valveMonitorConfig, err }) => {
  const infoMessage = "Deploy message";

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
        <div>
          <h3 className={styles.sectionTitle}>Fleet Controls</h3>
          <Row gutter={16}>
            <Col className={styles.motionFrequencyCard}>
              <MonitorFrequencyCard
                currentFrequency={valveMonitorConfig.monitorFrequency}
                setCurrentFrequency={() => {}}
                setErrorMessage={() => {}}
                setIsErrored={() => {}}
                setIsLoading={() => {}}
              />
            </Col>
          </Row>

          {Config.isBuildVersionSet() ? (
            <Alert description={infoMessage} type="info" closable />
          ) : null}
        </div>
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
