import { Card, Radio } from "antd";
import type { RadioChangeEvent } from "antd/lib/radio";
import { updateValveMonitorFrequency } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";
import monitorFrequencyCardStyles from "../../styles/MonitorFrequencyCard.module.scss";

export interface MonitorFrequencyProps {
  currentFrequency: number;
  setCurrentFrequency: (currentFrequency: number) => void;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
}

const MonitorFrequencyCard = (props: MonitorFrequencyProps) => {
  const {
    currentFrequency,
    setCurrentFrequency,
    setIsErrored,
    setIsLoading,
    setErrorMessage,
  } = props;

  const frequencyOptions = [
    { label: "2 Min", value: 2 },
    { label: "3 Min", value: 3 },
    { label: "5 Min", value: 5 },
  ];

  const updateFrequency = async ({ target: { value } }: RadioChangeEvent) => {
    if (value === currentFrequency) return;
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateValveMonitorFrequency(Number(value));
      setCurrentFrequency(Number(value));
    } catch (e) {
      setIsErrored(true);
      setErrorMessage("");
      setErrorMessage(ERROR_MESSAGE.UPDATE_FLEET_MONITOR_FREQUENCY_FAILED);
    }
    setIsLoading(false);
  };

  return (
    <Card
      className={cardStyles.cardContainer}
      title={
        <div className={cardStyles.cardTitle}>
          <div>Flow Monitoring Frequency</div>
        </div>
      }
    >
      <div className={monitorFrequencyCardStyles.radioContainer}>
        <Radio.Group
          options={frequencyOptions}
          value={currentFrequency || 2}
          // eslint-disable-next-line @typescript-eslint/no-misused-promises
          onChange={updateFrequency}
        />
      </div>
    </Card>
  );
};

export default MonitorFrequencyCard;
