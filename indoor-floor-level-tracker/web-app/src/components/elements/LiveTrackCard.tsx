import { Card, Switch } from "antd";
import cardStyles from "../../styles/Card.module.scss";
import liveTrackStyles from "../../styles/LiveTrackCard.module.scss";

export interface LiveTrackCardProps {
  liveTrackEnabled: boolean;
  toggleLiveTracking: (checked: boolean) => Promise<boolean>;
}

const LiveTrackCard = (props: LiveTrackCardProps) => {
  const { liveTrackEnabled, toggleLiveTracking } = props;

  return (
    <Card className={cardStyles.cardContainer}>
      <p>Enable Live Track</p>
      <div className={liveTrackStyles.toggleWrapper}>
        <Switch checked={liveTrackEnabled} onChange={toggleLiveTracking} />
      </div>
    </Card>
  );
};

export default LiveTrackCard;
