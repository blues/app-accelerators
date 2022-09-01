import { GetServerSideProps, NextPage } from "next";
import Title from "antd/lib/typography/Title";
import Link from "next/link";
import { Breadcrumb, Card, Space } from "antd";
import { services } from "../services/ServiceLocatorServer";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES, isError, MayError } from "../services/Errors";
import { Project } from "../services/AppModel";
import Config from "../../Config";
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
  const { devices } = project;

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
        Extract history from Notehub.io ➡️ Load into PostgreSQL database
      </Card>
      <Title level={3}>Devices ({`${devices?.length || "none"}`})</Title>
      {devices &&
        devices.map((device) => (
          <Card key={device.id.deviceUID}>
            <Title level={4}>
              <a href={`/${device.id.deviceUID}/details`}>
                {`${device.name || "?"}`}
              </a>
            </Title>
            Notehub{" "}
            <Link
              href={`${notehubProjectURL}/devices/${device.id.deviceUID}`}
            >
              <a target="_blank">{device.id.deviceUID}</a>
            </Link>
            <br />
            Last Seen <strong>{device.lastSeenAt}</strong>
            <br />
            Location <strong>{device.locationName}</strong>
          </Card>
        ))}
    </Space>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  try {
    const appService = services().getAppService();
    const bulkDataImportUrl = services().getUrlManager().bulkDataImport();
    const project = await appService.getProject();
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
