import { Card, Form, Input } from "antd";
import { updateAlarmThreshold } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";
// import monitorFrequencyCardStyles from "../../styles/MonitorFrequencyCard.module.scss";

export interface AlarmThresholdProps {
  currentMinFlow: number;
  currentMaxFlow: number;
  setCurrentMinFlow: (currentMinFlow: number) => void;
  setCurrentMaxFlow: (currentMaxFlow: number) => void;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
}

const AlarmThresholdCard = (props: AlarmThresholdProps) => {
  const {
    currentMinFlow,
    currentMaxFlow,
    setCurrentMinFlow,
    setCurrentMaxFlow,
    setIsErrored,
    setIsLoading,
    setErrorMessage,
  } = props;

  const [form] = Form.useForm();

  const update = async () => {
    const min = form.getFieldValue("min");
    const max = form.getFieldValue("max");
    if (min === currentMinFlow && max === currentMaxFlow) {
      return;
    }
    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateAlarmThreshold(Number(min), Number(max));
      setCurrentMinFlow(Number(min));
      setCurrentMaxFlow(Number(max));
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
          <div>Alarm Threshold</div>
        </div>
      }
    >
      <div className="">
        <p>Configure default flow rate range</p>
        <Form form={form}>
          <Form.Item name="min" label="Min mL/min">
            <Input placeholder="xx.x" onBlur={update} />
          </Form.Item>
          <Form.Item name="max" label="Max mL/min">
            <Input placeholder="xx.x" onBlur={update} />
          </Form.Item>
        </Form>
      </div>
    </Card>
  );
};

export default AlarmThresholdCard;
