import { useEffect, useRef } from "react";
import { GetServerSideProps, NextPage } from "next";
import { Carousel, Alert } from "antd";
import { CarouselRef } from "antd/lib/carousel";
import GatewayCard from "../components/elements/GatewayCard";
import { services } from "../services/ServiceLocatorServer";
import Gateway from "../services/alpha-models/Gateway";
import Node from "../services/alpha-models/Node";
import { ERROR_MESSAGE, getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import CarouselArrowFixRight from "../components/elements/CarouselArrowFixRight";
import CarouselArrowFixLeft from "../components/elements/CarouselArrowFixLeft";
import { getCombinedGatewayNodeInfo } from "../components/presentation/gatewayNodeInfo";
import styles from "../styles/Home.module.scss";
import Config from "../../config";

type HomeData = {
  gatewayNodeData: Gateway[];
  err?: string;
};

const Home: NextPage<HomeData> = ({ gatewayNodeData, err }) => {
  const carouselRef = useRef<CarouselRef>(null);

  const sparrowInfoMessage = (
    <span>
      You are viewing an example deployment of the Sparrow Reference Web App,
      showing live data from LoRa sensor nodes deployed throughout the United
      States. If you’re interested in building a dashboard like this yourself,
      check out{" "}
      <a href="https://github.com/blues/sparrow-reference-web-app">
        this project’s README on GitHub
      </a>
      .
    </span>
  );
  useEffect(() => {
    // auto focuses the carousel on component mount for keyboard accessibility
    carouselRef.current?.goTo(0);
  }, []);

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
            <Alert description={sparrowInfoMessage} type="info" closable />
          ) : null}

          <h2 data-testid="gateway-header" className={styles.sectionSubTitle}>
            Gateway
          </h2>
          <Carousel
            ref={carouselRef}
            focusOnSelect
            dots
            arrows
            nextArrow={<CarouselArrowFixRight />}
            prevArrow={<CarouselArrowFixLeft />}
          >
            {gatewayNodeData.map((gateway, index) => (
              <GatewayCard
                key={gateway.uid}
                index={index}
                gatewayDetails={gateway}
              />
            ))}
          </Carousel>
        </>
      )}
    </div>
  );
};
export default Home;

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  let gateways: Gateway[] = [];
  let latestNodeDataList: Node[] = [];
  let gatewayNodeData: Gateway[] = [];
  let err = "";

  try {
    const appService = services().getAppService();
    gateways = await appService.getGateways();
    latestNodeDataList = await appService.getNodes(
      gateways.map((gateway) => gateway.uid)
    );

    gatewayNodeData = getCombinedGatewayNodeInfo(latestNodeDataList, gateways);
    if (gatewayNodeData.length === 0) {
      err = ERROR_MESSAGE.NO_GATEWAYS_FOUND;
    }
    return {
      props: { gatewayNodeData, err },
    };
  } catch (e) {
    err = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { gatewayNodeData, err },
  };
};
