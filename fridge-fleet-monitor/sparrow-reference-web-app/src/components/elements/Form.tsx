import { Form } from "antd";
import { Store } from "antd/lib/form/interface";
import { ValidateErrorEntity } from "rc-field-form/lib/interface";
import styles from "../../styles/Form.module.scss";

export interface FormProps {
  label?: JSX.Element | string;
  tooltip?: string;
  name?: string;
  rules?: [
    {
      required: boolean;
      message: string;
    }
  ];
  contents: JSX.Element;
  initialValue?: string;
}

const FormComponent = ({
  formItems,
  onFinish,
  onFinishFailed,
}: {
  formItems: FormProps[];
  onFinish: (values: Store) => void;
  onFinishFailed: (values: ValidateErrorEntity) => void;
}) => {
  const [form] = Form.useForm();
  return (
    <Form
      form={form}
      layout="vertical"
      onFinish={(values: Store) => onFinish(values)}
      onFinishFailed={(values) => onFinishFailed(values)}
    >
      {formItems.map((formItem, index) => (
        <Form.Item
          // eslint-disable-next-line react/no-array-index-key
          key={index}
          label={formItem.label}
          name={formItem.name}
          tooltip={formItem.tooltip}
          rules={formItem.rules}
          className={styles.formLabel}
          initialValue={formItem.initialValue}
        >
          {formItem.contents}
        </Form.Item>
      ))}
    </Form>
  );
};

export default FormComponent;
