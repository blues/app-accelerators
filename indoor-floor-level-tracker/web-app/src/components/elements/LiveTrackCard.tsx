import { Card, Switch } from "antd";
import { updateLiveTrackerStatus } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";
import liveTrackStyles from "../../styles/LiveTrackCard.module.scss";

export interface LiveTrackCardProps {
  isLiveTrackingEnabled: boolean;
  setIsLiveTrackingEnabled: (isLiveTrackingEnabled: boolean) => void;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
}

const LiveTrackCard = (props: LiveTrackCardProps) => {
  const {
    isLiveTrackingEnabled,
    setIsLiveTrackingEnabled,
    setIsErrored,
    setIsLoading,
    setErrorMessage,
  } = props;

  const toggleLiveTracking = async (checked: boolean) => {
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateLiveTrackerStatus(checked);
      setIsLiveTrackingEnabled(checked);
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.UPDATE_FLEET_LIVE_STATUS_FAILED);
    }
    setIsLoading(false);
  };

  return (
    <Card className={cardStyles.cardContainer}>
      <p className={liveTrackStyles.toggleLabel}>Enable Live Track</p>
      <div className={liveTrackStyles.toggleWrapper}>
        <Switch checked={isLiveTrackingEnabled} onChange={toggleLiveTracking} />
      </div>
    </Card>
  );
};

export default LiveTrackCard;
