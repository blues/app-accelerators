import { InfoCircleFilled } from "@ant-design/icons";
import { Card, Form, InputNumber, Tooltip } from "antd";
import { updateAlarmThreshold } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";
import alarmThresholdCardStyles from "../../styles/AlarmThresholdCard.module.scss";

export interface AlarmThresholdProps {
  currentMinFlowThreshold: number | undefined;
  currentMaxFlowThreshold: number | undefined;
  setCurrentMinFlowThreshold: (currentMinFlow: number | undefined) => void;
  setCurrentMaxFlowThreshold: (currentMaxFlow: number | undefined) => void;
  setIsErrored: (isErrored: boolean) => void;
  setIsLoading: (isLoading: boolean) => void;
  setErrorMessage: (errorMessage: string) => void;
}

const AlarmThresholdCard = (props: AlarmThresholdProps) => {
  const {
    currentMinFlowThreshold,
    currentMaxFlowThreshold,
    setCurrentMinFlowThreshold,
    setCurrentMaxFlowThreshold,
    setIsErrored,
    setIsLoading,
    setErrorMessage,
  } = props;

  const [form] = Form.useForm();

  const update = async () => {
    const min = form.getFieldValue("min") || undefined;
    const max = form.getFieldValue("max") || undefined;

    if (min === currentMinFlowThreshold && max === currentMaxFlowThreshold) {
      return;
    }

    setIsErrored(false);
    setErrorMessage("");
    setIsLoading(true);

    try {
      await updateAlarmThreshold(min, max);
      setCurrentMinFlowThreshold(min);
      setCurrentMaxFlowThreshold(max);
    } catch (e) {
      setIsErrored(true);
      setErrorMessage(ERROR_MESSAGE.UPDATE_FLEET_ALARM_THRESHOLD_FAILED);
    }
    setIsLoading(false);
  };

  return (
    <Card
      className={cardStyles.cardContainer}
      title={
        <div className={cardStyles.cardTitle}>
          <div>Alarm Setting</div>
        </div>
      }
    >
      <div className={alarmThresholdCardStyles.cardBody}>
        <p>
          Configure fleet flow rate
          <Tooltip
            color="#416681"
            title="An alarm is triggered if the flow rate falls outside these values."
          >
            <InfoCircleFilled />
          </Tooltip>
        </p>
        <Form
          form={form}
          initialValues={{
            min: currentMinFlowThreshold,
            max: currentMaxFlowThreshold,
          }}
          onFinish={update}
          layout="vertical"
        >
          <Form.Item name="min" label="Min mL/min">
            <InputNumber
              placeholder="xx.x"
              onBlur={form.submit}
              onPressEnter={form.submit}
            />
          </Form.Item>
          <Form.Item name="max" label="Max mL/min">
            <InputNumber
              placeholder="xx.x"
              onBlur={form.submit}
              onPressEnter={form.submit}
            />
          </Form.Item>
        </Form>
      </div>
    </Card>
  );
};

export default AlarmThresholdCard;
