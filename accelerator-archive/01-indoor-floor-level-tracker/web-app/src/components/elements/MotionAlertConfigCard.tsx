import { Card, Radio } from "antd";
import type { RadioChangeEvent } from "antd/lib/radio";
import Image from "next/image";
import ClockIcon from "./images/clock.svg";
import { updateNoMovementThreshold } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";
import motionAlertCardStyles from "../../styles/MotionAlertCard.module.scss";

export interface MotionAlertCardConfigProps {
  currentNoMovementThreshold: number;
  setCurrentNoMovementThreshold: (currentNoMovementThreshold: number) => void;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
}

const MotionAlertCardConfigCard = (props: MotionAlertCardConfigProps) => {
  const {
    currentNoMovementThreshold,
    setCurrentNoMovementThreshold,
    setIsErrored,
    setIsLoading,
    setErrorMessage,
  } = props;

  const noMovementThresholdOptions = [
    { label: "1 Min", value: 60 },
    { label: "2 Min", value: 120 },
    { label: "3 Min", value: 180 },
  ];

  const Clock = <Image src={ClockIcon} alt="Clock" />;

  const updateNoMovementThresholdValue = async ({
    target: { value },
  }: RadioChangeEvent) => {
    if (value === currentNoMovementThreshold) return;
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateNoMovementThreshold(Number(value));
      setCurrentNoMovementThreshold(Number(value));
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(
        String(ERROR_MESSAGE.UPDATE_FLEET_NO_MOVEMENT_THRESHOLD_FAILED)
      );
    }
    setIsLoading(false);
  };

  return (
    <Card
      className={cardStyles.cardContainer}
      title={
        <div className={cardStyles.cardTitle}>
          <div className={cardStyles.titleImage}>{Clock}</div>
          <div>No Motion Alarm Time</div>
        </div>
      }
    >
      <div className={motionAlertCardStyles.radioContainer}>
        <Radio.Group
          options={noMovementThresholdOptions}
          value={currentNoMovementThreshold || 120}
          // eslint-disable-next-line @typescript-eslint/no-misused-promises
          onChange={updateNoMovementThresholdValue}
        />
      </div>
    </Card>
  );
};

export default MotionAlertCardConfigCard;
