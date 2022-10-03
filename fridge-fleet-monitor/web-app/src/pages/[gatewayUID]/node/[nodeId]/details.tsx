import { GetServerSideProps, NextPage } from "next";
import Image from "next/image";
import { useRouter } from "next/router";
import { Card, Input, Button, Tabs, Row, Col, Tooltip, Select } from "antd";
import axios from "axios";
import { Store } from "antd/lib/form/interface";
import { ValidateErrorEntity } from "rc-field-form/lib/interface";
import { ParsedUrlQuery } from "querystring";
import Form, { FormProps } from "../../../../components/elements/Form";
import {
  getErrorMessage,
  HISTORICAL_SENSOR_DATA_MESSAGE,
  NODE_MESSAGE,
} from "../../../../constants/ui";
import { services } from "../../../../services/ServiceLocatorServer";
import NodeDetailsLineChart from "../../../../components/charts/NodeDetailsLineChart";
import NodeDetailsBarChart from "../../../../components/charts/NodeDetailsBarChart";
import NodeDetailViewModel from "../../../../models/NodeDetailViewModel";
import { getNodeDetailsPresentation } from "../../../../components/presentation/nodeDetails";
import { ERROR_CODES } from "../../../../services/Errors";
import TemperatureSensorSchema from "../../../../services/alpha-models/readings/TemperatureSensorSchema";
import HumiditySensorSchema from "../../../../services/alpha-models/readings/HumiditySensorSchema";
import VoltageSensorSchema from "../../../../services/alpha-models/readings/VoltageSensorSchema";
import PressureSensorSchema from "../../../../services/alpha-models/readings/PressureSensorSchema";
import ContactSwitchSensorSchema from "../../../../services/alpha-models/readings/ContactSwitchSensorSchema";
import styles from "../../../../styles/Home.module.scss";
import detailsStyles from "../../../../styles/Details.module.scss";

// custom interface to avoid UI believing query params can be undefined when they can't be
interface SparrowQueryInterface extends ParsedUrlQuery {
  gatewayUID: string;
  nodeId: string;
  minutesBeforeNow?: string; // this value is a string so it can be a query param
  settings?: string;
}

type NodeDetailsData = {
  viewModel: NodeDetailViewModel;
  err?: string;
};

const DEFAULT_MINUTES_BEFORE_NOW = "1440";

const NodeDetails: NextPage<NodeDetailsData> = ({ viewModel, err }) => {
  const { TabPane } = Tabs;
  const { Option } = Select;
  const { query } = useRouter();

  // neither of these values will ever be null because the URL path depends on them to render this page
  const { gatewayUID, nodeId, settings } = query as SparrowQueryInterface;
  // todo - can we use the UrlManager here?
  const nodeUrl = `/${gatewayUID}/node/${nodeId}/details`;

  const router = useRouter();
  // Call this function whenever you want to
  // refresh props!
  const refreshData = async () => {
    await router.replace(router.asPath);
  };

  const handleDateRangeChange = async (value: string) => {
    // call this function to force a page update with new chart date range
    await router.replace({
      pathname: `${nodeUrl}`,
      query: { minutesBeforeNow: value },
    });
  };

  const formItems: FormProps[] = [
    {
      label: (
        <h3
          data-testid="current-readings"
          className={detailsStyles.tabSectionTitle}
        >
          Current Readings
        </h3>
      ),
      contents: (
        <div className={detailsStyles.nodeFormTimestamp}>
          Last updated{` `}
          {viewModel.node?.lastActivity}
        </div>
      ),
    },
    {
      label: "Name",
      name: "name",
      tooltip: "What is the name of your node?",
      initialValue:
        viewModel.node?.name !== NODE_MESSAGE.NO_NAME
          ? viewModel.node?.name
          : undefined,
      rules: [{ required: true, message: "Please add the name of your node" }],
      contents: (
        <Input
          data-testid="form-input-node-name"
          placeholder="Name of node"
          maxLength={49}
          showCount
        />
      ),
    },
    {
      label: "Location",
      name: "location",
      tooltip: "Where is your node located?",
      initialValue:
        viewModel.node?.location !== NODE_MESSAGE.NO_LOCATION
          ? viewModel.node?.location
          : undefined,
      rules: [
        { required: true, message: "Please add the location of your node" },
      ],
      contents: (
        <Input
          data-testid="form-input-node-location"
          placeholder="Node location"
          maxLength={15}
          showCount
        />
      ),
    },
    {
      contents: (
        <Button data-testid="form-submit" htmlType="submit" type="primary">
          Save Changes
        </Button>
      ),
    },
  ];

  const formOnFinish = async (values: Store) => {
    // TODO: Move this to the app service / data provider
    const response = await axios.post(
      `/api/gateway/${gatewayUID}/node/${nodeId}/config`,
      values
    );
    console.log(`Success`);
    console.log(response);

    if (response.status < 300) {
      await refreshData();
    }
  };

  const formOnFinishFailed = (errorInfo: ValidateErrorEntity) => {
    console.log("Failed:", errorInfo);
  };

  return (
    <>
      {err && (
        <h2
          className={styles.errorMessage}
          dangerouslySetInnerHTML={{ __html: err }}
        />
      )}
      {viewModel.node && (
        <div>
          <h2 data-testid="node-name" className={styles.sectionTitle}>
            Node:{` `}
            {viewModel.node.name}
          </h2>
          <h3
            data-testid="node-gateway-name"
            className={styles.sectionSubHeader}
          >
            Gateway:{` `}
            {viewModel?.gateway?.name && viewModel.gateway.name}
          </h3>
          <Tabs defaultActiveKey={settings ? "2" : "1"}>
            <TabPane tab="Details" key="1">
              <h3
                data-testid="current-readings"
                className={detailsStyles.tabSectionTitle}
              >
                Current Readings
              </h3>
              <p
                data-testid="last-seen"
                className={detailsStyles.nodeTimestamp}
              >
                Last updated {viewModel.node.lastActivity}
              </p>
              <div className={detailsStyles.signalStrength}>
                <span
                  data-testid="current-voltage"
                  className={detailsStyles.voltage}
                >
                  Voltage{` `}
                  {viewModel?.node?.voltage}
                </span>
                {viewModel.node?.barsIconPath ? (
                  <Tooltip
                    title={`LoRa signal: ${viewModel.node.barsTooltip || ""}`}
                  >
                    <Image
                      src={viewModel.node.barsIconPath}
                      width={20}
                      alt="Node Lora signal strength"
                      data-testid="signal-strength"
                    />
                  </Tooltip>
                ) : null}
              </div>

              <Row
                justify="start"
                className={detailsStyles.currentReadingsRow}
                gutter={[8, 16]}
              >
                <Col xs={12} sm={12} lg={5}>
                  <Card
                    className={detailsStyles.card}
                    data-testid="temperature"
                  >
                    Temperature
                    <br />
                    <span className={detailsStyles.dataNumber}>
                      {viewModel.node.temperature}
                    </span>
                  </Card>
                </Col>
                <Col xs={12} sm={12} lg={5}>
                  <Card className={detailsStyles.card} data-testid="humidity">
                    Humidity
                    <br />
                    <span className={detailsStyles.dataNumber}>
                      {viewModel.node.humidity}
                    </span>
                  </Card>
                </Col>
                <Col xs={12} sm={12} lg={4}>
                  <Card className={detailsStyles.card} data-testid="voltage">
                    Voltage
                    <br />
                    <span className={detailsStyles.dataNumber}>
                      {viewModel.node.voltage}
                    </span>
                  </Card>
                </Col>
                <Col xs={12} sm={12} lg={5}>
                  <Card className={detailsStyles.card} data-testid="pressure">
                    Pressure
                    <br />
                    <span className={detailsStyles.dataNumber}>
                      {viewModel.node.pressure}
                    </span>
                  </Card>
                </Col>
                <Col xs={12} sm={12} lg={5}>
                  <Card
                    className={detailsStyles.card}
                    data-testid="door-status"
                  >
                    Door Status
                    <br />
                    <span className={detailsStyles.dataNumber}>
                      {viewModel.node.doorStatus}
                    </span>
                  </Card>
                </Col>
              </Row>
              <Row>
                <Col xs={12} sm={12} md={8} lg={8}>
                  <p className={detailsStyles.dateRangeLabel}>
                    Chart date range
                  </p>
                  <Select
                    data-testid="date-range-picker"
                    className={detailsStyles.currentReadingsRow}
                    defaultValue={
                      query.minutesBeforeNow
                        ? query.minutesBeforeNow.toString()
                        : DEFAULT_MINUTES_BEFORE_NOW
                    }
                    style={{ width: "100%" }}
                    onChange={handleDateRangeChange}
                  >
                    <Option value="60">Last 1 hour</Option>
                    <Option value="720">Last 12 hours</Option>
                    <Option value="1440">Last 24 hours</Option>
                    <Option value="2880">Last 2 days</Option>
                    <Option value="4320">Last 3 days</Option>
                    <Option value="7200">Last 5 days</Option>
                    <Option value="10080">Last 7 days</Option>
                  </Select>
                </Col>
              </Row>
              <Row justify="start" gutter={[8, 16]}>
                <Col xs={24} sm={24} lg={12}>
                  <Card className={detailsStyles.nodeChart}>
                    <h3>Temperature</h3>
                    <p
                      data-testid="last-seen-temperature"
                      className={detailsStyles.nodeChartTimestamp}
                    >
                      Last updated {viewModel.node.lastActivity}
                    </p>
                    {viewModel.readings?.temperature.length ? (
                      <NodeDetailsLineChart
                        label="Temperature"
                        data={viewModel.readings.temperature}
                        chartColor="#59d2ff"
                        schema={TemperatureSensorSchema}
                      />
                    ) : (
                      HISTORICAL_SENSOR_DATA_MESSAGE.NO_TEMPERATURE_HISTORY
                    )}
                  </Card>
                </Col>
                <Col xs={24} sm={24} lg={12}>
                  <Card className={detailsStyles.nodeChart}>
                    <h3>Humidity</h3>
                    <p
                      data-testid="last-seen-humidity"
                      className={detailsStyles.nodeChartTimestamp}
                    >
                      Last updated {viewModel.node.lastActivity}
                    </p>
                    {viewModel.readings?.humidity.length ? (
                      <NodeDetailsLineChart
                        label="Humidity"
                        data={viewModel.readings.humidity}
                        chartColor="#ba68c8"
                        schema={HumiditySensorSchema}
                      />
                    ) : (
                      HISTORICAL_SENSOR_DATA_MESSAGE.NO_HUMIDITY_HISTORY
                    )}
                  </Card>
                </Col>
                <Col xs={24} sm={24} lg={12}>
                  <Card className={detailsStyles.nodeChart}>
                    <h3>Voltage</h3>
                    <p
                      data-testid="last-seen-voltage"
                      className={detailsStyles.nodeChartTimestamp}
                    >
                      Last updated {viewModel.node.lastActivity}
                    </p>
                    {viewModel.readings?.voltage.length ? (
                      <NodeDetailsLineChart
                        label="Voltage"
                        data={viewModel.readings.voltage}
                        chartColor="#9ccc65"
                        schema={VoltageSensorSchema}
                      />
                    ) : (
                      HISTORICAL_SENSOR_DATA_MESSAGE.NO_VOLTAGE_HISTORY
                    )}
                  </Card>
                </Col>
                <Col xs={24} sm={24} lg={12}>
                  <Card className={detailsStyles.nodeChart}>
                    <h3>Pressure</h3>
                    <p
                      data-testid="last-seen-pressure"
                      className={detailsStyles.nodeChartTimestamp}
                    >
                      Last updated {viewModel.node.lastActivity}
                    </p>
                    {viewModel.readings?.pressure.length ? (
                      <NodeDetailsLineChart
                        label="Pressure"
                        data={viewModel.readings.pressure}
                        chartColor="#ffd54f"
                        schema={PressureSensorSchema}
                      />
                    ) : (
                      HISTORICAL_SENSOR_DATA_MESSAGE.NO_PRESSURE_HISTORY
                    )}
                  </Card>
                </Col>
                <Col xs={24} sm={24} lg={12}>
                  <Card className={detailsStyles.nodeChart}>
                    <h3>Door Status</h3>
                    <p
                      data-testid="last-seen-count"
                      className={detailsStyles.nodeChartTimestamp}
                    >
                      Last updated {viewModel.node.lastActivity}
                    </p>
                    {viewModel.readings?.doorStatus.length ? (
                      <NodeDetailsBarChart
                        label="Door Status"
                        data={viewModel.readings.doorStatus}
                        chartColor="#ff7e6d"
                        schema={ContactSwitchSensorSchema}
                      />
                    ) : (
                      HISTORICAL_SENSOR_DATA_MESSAGE.NO_DOOR_STATUS_HISTORY
                    )}
                  </Card>
                </Col>
              </Row>
            </TabPane>
            <TabPane tab="Settings" key="2">
              <Form
                formItems={formItems}
                onFinish={formOnFinish}
                onFinishFailed={formOnFinishFailed}
              />
            </TabPane>
          </Tabs>
        </div>
      )}
    </>
  );
};

export default NodeDetails;

export const getServerSideProps: GetServerSideProps<NodeDetailsData> = async ({
  query,
}) => {
  const { gatewayUID, nodeId, minutesBeforeNow } =
    query as SparrowQueryInterface;
  const appService = services().getAppService();
  let viewModel: NodeDetailViewModel = {};
  let err = "";

  try {
    const gateway = await appService.getGateway(gatewayUID);
    const node = await appService.getNode(gatewayUID, nodeId);
    const readings = await appService.getNodeData(
      gatewayUID,
      nodeId,
      Number(minutesBeforeNow || DEFAULT_MINUTES_BEFORE_NOW)
    );

    viewModel = getNodeDetailsPresentation(node, gateway, readings);
  } catch (e) {
    err = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { viewModel, err },
  };
};
