import { useState } from "react";
import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { Row, Col, Alert, Card, InputNumber } from "antd";
import { Store } from "antd/lib/form/interface";
import { ValidateErrorEntity } from "rc-field-form/lib/interface";
import { TrackerConfig } from "../services/ClientModel";
import { ERROR_MESSAGE, getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import { services } from "../services/ServiceLocatorServer";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import Form, { FormProps } from "../components/elements/Form";
import { updateFloorHeightConfig } from "../api-client/fleetVariables";
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
      initialValue: fleetTrackerConfig.floorHeight
        ? `${JSON.stringify(fleetTrackerConfig.floorHeight)}`
        : "Input here...",
      rules: [{ required: true, message: "Please add only numbers." }],
      contents: (
        <InputNumber size="large" min={1} max={100000} controls={false} />
      ),
    },
  ];

  const formOnFinish = async (values: Store) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateFloorHeightConfig(values.floorHeight);
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.UPDATE_FLEET_FLOOR_HEIGHT_CONFIG_FAILED);
    }
    setIsLoading(false);
    refreshData();
  };

  const formOnFinishFailed = (errorInfo: ValidateErrorEntity) => {
    console.log("Failed:", errorInfo);
  };

  return (
    <div className={styles.container}>
      {error ? (
        <h2
          className={styles.errorMessage}
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
