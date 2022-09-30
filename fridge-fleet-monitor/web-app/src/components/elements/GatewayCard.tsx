import { useRouter } from "next/router";
import Image from "next/image";
import { Card, Col, Row, Tooltip, Typography } from "antd";
import Gateway from "../../services/alpha-models/Gateway";
import NodeCard from "./NodeCard";
import { getFormattedLastSeen } from "../presentation/uiHelpers";
import { GATEWAY_MESSAGE, ERROR_MESSAGE } from "../../constants/ui";
import styles from "../../styles/Home.module.scss";
import cardStyles from "../../styles/Card.module.scss";

interface GatewayProps {
  gatewayDetails: Gateway;
  index: number;
}

const GatewayCardComponent = (props: GatewayProps) => {
  const { gatewayDetails, index } = props;
  const { Text } = Typography;

  const router = useRouter();
  const gatewayUrl = `/${gatewayDetails.uid}/details`;
  const handleCardClick = (e: React.MouseEvent<HTMLElement>) => {
    e.preventDefault();
    // eslint-disable-next-line @typescript-eslint/no-floating-promises
    router.push(gatewayUrl);
  };

  const formattedLocation = gatewayDetails?.location
    ? gatewayDetails.location
    : GATEWAY_MESSAGE.NO_LOCATION;

  return (
    <>
      <Row gutter={[16, 16]}>
        <Col xs={24} sm={24} lg={12}>
          <Card
            headStyle={{ padding: "0" }}
            className={cardStyles.gatewayCardStyle}
            hoverable
            onClick={handleCardClick}
            title={
              <div className={cardStyles.headerSection}>
                <span>
                  <Text
                    ellipsis={{
                      // eslint-disable-next-line @typescript-eslint/restrict-template-expressions
                      tooltip: `${gatewayDetails.name}`,
                    }}
                    data-testid={`gateway[${index}]-details`}
                  >
                    {gatewayDetails.name}
                  </Text>
                  <span className={cardStyles.timestamp}>
                    Last updated{` `}
                    {getFormattedLastSeen(gatewayDetails.lastActivity)}
                  </span>
                  <div
                    data-testid="gateway-location"
                    className={cardStyles.locationWrapper}
                  >
                    <span className={cardStyles.locationTitle}>
                      Location{` `}
                    </span>
                    <span className={cardStyles.location}>
                      {formattedLocation}
                    </span>
                  </div>
                </span>
                <span>
                  {gatewayDetails.cellIconPath && gatewayDetails.cellTooltip ? (
                    <Tooltip
                      title={`Cell signal: ${gatewayDetails.cellTooltip}`}
                    >
                      <Image
                        src={gatewayDetails.cellIconPath}
                        width={24}
                        alt="Gateway cell signal strength"
                      />
                    </Tooltip>
                  ) : null}
                  {gatewayDetails.wifiIconPath && gatewayDetails.wifiTooltip ? (
                    <Tooltip
                      title={`Wi-Fi signal: ${gatewayDetails.wifiTooltip}`}
                    >
                      <Image
                        src={gatewayDetails.wifiIconPath}
                        width={24}
                        alt="Gateway Wi-Fi signal strength"
                      />
                    </Tooltip>
                  ) : null}
                </span>
              </div>
            }
          />
        </Col>
      </Row>

      <h2 data-testid="node-header" className={styles.sectionSubTitle}>
        Nodes
      </h2>
      {gatewayDetails.nodeList.length ? (
        <Row gutter={[16, 16]}>
          {gatewayDetails.nodeList.map((node, cardIndex) => (
            <Col xs={24} sm={24} lg={12} key={node.nodeId}>
              <NodeCard index={cardIndex} nodeDetails={node} />
            </Col>
          ))}
        </Row>
      ) : (
        <h4 className={styles.errorMessage}>{ERROR_MESSAGE.NODES_NOT_FOUND}</h4>
      )}
    </>
  );
};

export default GatewayCardComponent;
