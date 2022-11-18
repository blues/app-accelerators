import { GetServerSideProps, NextPage } from "next";
import { Alert, Col, Row } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { useValveMonitorDeviceData } from "../hooks/useValveMonitorDeviceData";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import { ValveMonitorDevice } from "../services/AppModel";
import ValveMonitorTable from "../components/elements/ValveMonitorTable";
import Config from "../../Config";
import styles from "../styles/Home.module.scss";

type HomeData = {
  err?: string;
};

const Home: NextPage<HomeData> = ({ err }) => {
  const infoMessage = "Deploy message";

  const MS_REFETCH_INTERVAL = 60000;
  const { error: valveMonitorDevicesError, data: valveMonitorDeviceList } =
    useValveMonitorDeviceData(MS_REFETCH_INTERVAL);

  const error =
    valveMonitorDevicesError &&
    getErrorMessage(valveMonitorDevicesError.message);

  const valveMonitorDevices: ValveMonitorDevice[] | undefined =
    valveMonitorDeviceList;
  console.log("table data-----", valveMonitorDevices);

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
        <div>
          {valveMonitorDevices && (
            <>
              <h3 className={styles.sectionTitle}>Fleet Controls</h3>
              <Row gutter={16} />
              <Row gutter={[16, 24]}>
                <Col span={24}>
                  <div className={styles.tableHeaderRow}>
                    <h3 className={styles.sectionTitle}>Individual Controls</h3>
                  </div>
                </Col>
                <Col xs={12} sm={7} md={6} lg={4} />
              </Row>
              <Row gutter={[16, 24]}>
                <Col span={24}>
                  <ValveMonitorTable data={valveMonitorDeviceList} />
                </Col>
              </Row>
            </>
          )}
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
