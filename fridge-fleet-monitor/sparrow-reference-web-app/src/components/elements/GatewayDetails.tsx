import Image from "next/image";
import { Col, Card, Row, Tooltip } from "antd";
import EditInPlace from "./EditInPlace";
import NodeCard from "./NodeCard";
import { ERROR_MESSAGE } from "../../constants/ui";
import GatewayDetailViewModel from "../../models/GatewayDetailViewModel";
import styles from "../../styles/Home.module.scss";
import detailsStyles from "../../styles/Details.module.scss";

type GatewayDetailsData = {
  // eslint-disable-next-line react/require-default-props
  err?: string;
  onChangeName: (name: string) => Promise<boolean>;
  viewModel: GatewayDetailViewModel;
};

const GatewayDetails = ({
  err,
  onChangeName,
  viewModel,
}: GatewayDetailsData) => (
  <>
    {err && (
      <h2
        className={styles.errorMessage}
        dangerouslySetInnerHTML={{ __html: err }}
      />
    )}

    {viewModel.gateway && (
      <div>
        <h2
          data-testid="gateway-details-header"
          className={`${styles.sectionTitle} ${detailsStyles.editableHeading}`}
        >
          <span>Gateway:</span>
          <EditInPlace
            onChange={onChangeName}
            initialText={viewModel.gateway?.name}
            errorMessage={ERROR_MESSAGE.GATEWAY_NAME_CHANGE_FAILED}
            enabled={!viewModel.readOnly}
          />
        </h2>

        <div className={styles.container}>
          <div
            data-testid="gateway-last-seen"
            className={detailsStyles.timestamp}
          >
            Last seen {viewModel.gateway.lastActivity}
          </div>
          <div
            data-testid="gateway-signal-strength"
            className={detailsStyles.signalStrength}
          >
            {viewModel.gateway.cellBarsIconPath &&
            viewModel.gateway.cellBarsTooltip ? (
              <Tooltip
                title={`Cell signal: ${viewModel.gateway.cellBarsTooltip}`}
              >
                <Image
                  src={viewModel.gateway.cellBarsIconPath}
                  width={24}
                  height={24}
                  alt="Gateway cell signal strength"
                />
              </Tooltip>
            ) : null}
            {viewModel.gateway.wifiBarsIconPath &&
            viewModel.gateway.wifiBarsTooltip ? (
              <Tooltip
                title={`Wi-Fi signal: ${viewModel.gateway.wifiBarsTooltip}`}
              >
                <Image
                  src={viewModel.gateway.wifiBarsIconPath}
                  width={24}
                  alt="Gateway Wi Fi signal strength"
                />
              </Tooltip>
            ) : null}
          </div>

          <Row gutter={[16, 16]}>
            <Col xs={12} sm={12} lg={8}>
              <Card className={detailsStyles.card}>
                <div className={detailsStyles.cardTitle}>Location</div>
                <span
                  data-testid="gateway-location"
                  className={detailsStyles.dataNumber}
                >
                  {viewModel.gateway.location}
                </span>
              </Card>
            </Col>
          </Row>

          {viewModel.nodes && viewModel.nodes.length > 0 ? (
            <>
              <h3
                data-testid="gateway-node-header"
                className={styles.sectionSubTitle}
              >
                Nodes
              </h3>
              <Row gutter={[16, 16]}>
                {viewModel.nodes.map((node, index) => (
                  <Col xs={24} sm={24} lg={12} key={node.nodeId}>
                    <NodeCard index={index} nodeDetails={node} />
                  </Col>
                ))}
              </Row>
            </>
          ) : (
            <h4 className={styles.errorMessage}>
              {ERROR_MESSAGE.NODES_NOT_FOUND}
            </h4>
          )}
        </div>
      </div>
    )}
  </>
);

export default GatewayDetails;
