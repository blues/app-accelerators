import { useState } from "react";
import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { Row, Col, Alert, Card, InputNumber } from "antd";
import { TrackerConfig } from "../services/ClientModel";
import { getErrorMessage } from "../constants/ui";
import { services } from "../services/ServiceLocatorServer";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import Form, { FormProps } from "../components/elements/Form";
import styles from "../styles/Settings.module.scss";
import homeStyles from "../styles/Home.module.scss";

type SettingsData = {
  fleetTrackerConfig: TrackerConfig;
  error: string;
};

const SettingsPage: NextPage<SettingsData> = ({
  fleetTrackerConfig,
  error,
}) => {
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [isErrored, setIsErrored] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string>("");

  const router = useRouter();
  // refresh the page
  const refreshData = () => {
    // eslint-disable-next-line no-void
    void router.replace(router.asPath);
  };

  console.log(fleetTrackerConfig);

  const formItems: FormProps[] = [
    {
      name: "floorHeight",
      label: <p>Configure Floor Height</p>,
      initialValue: "Default 4.2672 meters",
      rules: [{ required: true, message: "Please add only numbers." }],
      contents: (
        <InputNumber size="large" min={1} max={100000} controls={false} />
      ),
    },
  ];

  const formOnFinish = async (values: Store) => {
    // TODO: Move this to the app service / data provider
    console.log(values);
    console.log(`Success`);

    // if (response.status < 300) {
    //   await refreshData();
    // }
  };

  const formOnFinishFailed = (errorInfo: ValidateErrorEntity) => {
    console.log("Failed:", errorInfo);
  };

  return (
    <div className={styles.container}>
      {error ? (
        <h2
          className={styles.errorMessage}
          // life in the fast lane...
          // eslint-disable-next-line react/no-danger
          dangerouslySetInnerHTML={{ __html: error }}
        />
      ) : (
        <LoadingSpinner isLoading={isLoading}>
          <div>
            {isErrored && (
              <Alert type="error" message={errorMessage} closable />
            )}
            {fleetTrackerConfig && (
              <div className={homeStyles.container}>
                <h3 className={homeStyles.sectionTitle}>Settings</h3>
                <Row className={styles.settingsContainer}>
                  <Col xs={24} sm={24} md={22} lg={20}>
                    <Card
                      className={styles.settingsCard}
                      title="Floor Height"
                      bordered
                    >
                      <Form
                        formItems={formItems}
                        onFinish={formOnFinish}
                        onFinishFailed={formOnFinishFailed}
                      />
                    </Card>
                  </Col>
                </Row>
              </div>
            )}
          </div>
        </LoadingSpinner>
      )}
    </div>
  );
};

export default SettingsPage;

export const getServerSideProps: GetServerSideProps<
  SettingsData
> = async () => {
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
