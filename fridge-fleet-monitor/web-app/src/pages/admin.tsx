import { GetServerSideProps, NextPage } from "next";
import Title from "antd/lib/typography/Title";
import Link from "next/link";
import { Breadcrumb, Card, Space } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES, isError, MayError } from "../services/Errors";
import { Project } from "../services/AppModel";
import Config from "../../config";
// CSS
import styles from "../styles/Home.module.scss";

type HomeData = MayError<
  {
    project: Project;
    bulkDataImportUrl: string;
    notehubProjectURL: string;
  },
  string
>;

const Home: NextPage<HomeData> = (homeData: HomeData) => {
  if (isError(homeData)) {
    const { err } = homeData;
    return (
      <h2
        className={styles.errorMessage}
        dangerouslySetInnerHTML={{ __html: err }}
      />
    );
  }

  const { project, bulkDataImportUrl, notehubProjectURL } = homeData;
  const { gateways } = project;

  return (
    <Space direction="vertical" size="middle">
      <Breadcrumb>
        <Breadcrumb.Item>
          <Link href="/">Home</Link>
        </Breadcrumb.Item>
        <Breadcrumb.Item>
          <Link href="/admin">Admin</Link>
        </Breadcrumb.Item>
      </Breadcrumb>
      <Title level={2}>Admin Portal</Title>
      <Card>
        <Link href={notehubProjectURL}>
          <a target="_blank">Notehub.io Project</a>
        </Link>
      </Card>
      <Card>
        <Link href={bulkDataImportUrl}>
          <a>Bulk data import</a>
        </Link>
        <br />
        Extract history from Notehub.io ➡️ Load into Sparrow PostgreSQL database
      </Card>
      <Title level={3}>Gateways ({`${gateways?.length || "none"}`})</Title>
      {gateways &&
        gateways.map((gateway) => (
          <Card key={gateway.id.gatewayDeviceUID}>
            <Title level={4}>
              <a href={`/${gateway.id.gatewayDeviceUID}/details`}>
                {`${gateway.name || "?"}`}
              </a>
            </Title>
            Notehub{" "}
            <Link
              href={`${notehubProjectURL}/devices/${gateway.id.gatewayDeviceUID}`}
            >
              <a target="_blank">{gateway.id.gatewayDeviceUID}</a>
            </Link>
            <br />
            Last Seen <strong>{gateway.lastSeen}</strong>
            <br />
            Location <strong>{gateway.locationName}</strong>
          </Card>
        ))}
    </Space>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  try {
    const appService = services().getAppService();
    const project = await appService.getLatestProjectReadings();
    const bulkDataImportUrl = services().getUrlManager().bulkDataImport();
    const notehubProjectURL = services()
      .getUrlManager()
      .notehubProject(Config.hubGuiURL, Config.hubProjectUID);

    return {
      props: { project, bulkDataImportUrl, notehubProjectURL },
    };
  } catch (err) {
    if (err instanceof Error) {
      return {
        props: {
          err: getErrorMessage(err.message),
        },
      };
    }
    return {
      props: {
        err: getErrorMessage(ERROR_CODES.INTERNAL_ERROR),
      },
    };
  }
};
