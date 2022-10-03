import { useRouter } from "next/router";
import { Card, Row, Col, Typography } from "antd";
import styles from "../../styles/Card.module.scss";
import { Gateway, Node, Reading, SensorType } from "../../services/AppModel";
import { NODE_MESSAGE } from "../../constants/ui";
import { getFormattedLastSeenDate } from "../presentation/uiHelpers";
import registry from "../renderers/client";
import { ReadingVisualization } from "../renderers/registry";
import TextReadingRenderer from "./TextReadingRenderer";
import TextReadingRendererComponent from "./TextReadingRenderer";

interface NodeProps {
  gateway: Gateway;
  node: Node;
  index: number;
}

const NodeCardComponent = (props: NodeProps) => {
  const { gateway, node, index } = props;
  const { Text } = Typography;

  const router = useRouter();
  // todo - use urlManager to construct the URL from the gateway/node.
  const nodeUrl = `/${gateway.id.gatewayDeviceUID}/node/${node.id.nodeID}/details`;
  const handleCardClick = (e: React.MouseEvent<HTMLElement>) => {
    e.preventDefault();
    // eslint-disable-next-line @typescript-eslint/no-floating-promises
    router.push(nodeUrl);
  };

  const name = node.name || NODE_MESSAGE.NO_NAME;

  // todo - fix up location. Either as a property on Node or as a known sensor type
  const location = NODE_MESSAGE.NO_LOCATION;
  const lastActivity = node.lastSeen
    ? getFormattedLastSeenDate(new Date(node.lastSeen))
    : NODE_MESSAGE.NEVER_SEEN;
  const nodeReadings = node.currentReadings || [];

  const sensorRenders = nodeReadings.map((readingAndType, index) => {
    console.log("rendering reading ", readingAndType);
    let Renderer = registry.findRenderer(
      readingAndType.sensorType,
      ReadingVisualization.CARD
    );
    if (!Renderer) {
      console.log("no renderer for ", readingAndType.sensorType.name);
      Renderer = TextReadingRendererComponent;
    }
    const content = (
      <Renderer
        sensorType={readingAndType.sensorType}
        reading={readingAndType.reading}
        node={node}
        gateway={gateway}
      />
    );

    return content === null ? (
      content
    ) : (
      <Col key={index} xs={8} sm={5} md={5} lg={8}>
        {content}
      </Col>
    );
  });

  return (
    <Card
      headStyle={{ padding: "0" }}
      bodyStyle={{ padding: "0" }}
      className={styles.cardStyle}
      onClick={handleCardClick}
      hoverable
      title={
        <>
          <Text
            ellipsis={{
              // eslint-disable-next-line @typescript-eslint/restrict-template-expressions
              tooltip: `${name}`,
            }}
            data-testid={`node[${index}]-summary`}
          >
            {name}
          </Text>
          <span data-testid="node-timestamp" className={styles.timestamp}>
            Last updated{` `}
            {lastActivity}
          </span>
          <div data-testid="node-location" className={styles.locationWrapper}>
            <span className={styles.locationTitle}>Location{` `}</span>
            <span className={styles.location}>{location}</span>
          </div>
        </>
      }
    >
      <Row
        justify="start"
        gutter={[16, 16]}
        className={styles.cardContentsSensor}
      >
        <>{sensorRenders}</>
      </Row>
    </Card>
  );
};

export default NodeCardComponent;
