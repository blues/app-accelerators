import { GetServerSideProps, NextPage } from "next";
import { Alert } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import styles from "../styles/Home.module.scss";
import Config from "../../Config";

type HomeData = {
  err?: string;
};

const Home: NextPage<HomeData> = ({ err }) => {
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
        <>
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
  // just to get something on screen soon
  let devices: any = [];
  const deviceEvents: any = [];

  try {
    const appService = services().getAppService();
    devices = await appService.getDevices();
    console.log(devices);

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
