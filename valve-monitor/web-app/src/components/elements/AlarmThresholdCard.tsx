import { InfoCircleOutlined } from "@ant-design/icons";
import { Card, Form, InputNumber, Tooltip } from "antd";
import { updateAlarmThreshold } from "../../api-client/fleetVariables";
import { ERROR_MESSAGE } from "../../constants/ui";
import cardStyles from "../../styles/Card.module.scss";

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
      <div>
        <p>
          Configure default flow rate range
          <Tooltip title="Flow rate readings with a value outside of your configured range will generate an alarm.">
            <InfoCircleOutlined />
          </Tooltip>
        </p>
        <Form
          form={form}
          initialValues={{
            min: currentMinFlowThreshold,
            max: currentMaxFlowThreshold,
          }}
          onFinish={update}
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
