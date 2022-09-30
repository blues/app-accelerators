import { CheckOutlined, CloseOutlined, EditOutlined } from "@ant-design/icons";
import { Button, Form, Input } from "antd";
import React, { useState } from "react";
import styles from "../../styles/EditInPlace.module.scss";

type EditInPlaceData = {
  initialText: string;
  onChange: (updatedText: string) => Promise<boolean>;
  errorMessage: string;
  enabled: boolean;
};

const EditInPlace = ({
  initialText,
  onChange,
  errorMessage,
  enabled,
}: EditInPlaceData) => {
  const [isEditing, setIsEditing] = useState(false);
  const [isErred, setIsErred] = useState(false);

  const toggleEditing = () => {
    setIsEditing(!isEditing);
  };
  type EditFormData = { value: string };
  const onFinish = async (values: EditFormData) => {
    const { value } = values;
    const result = await onChange(value);
    setIsErred(!result);
    if (result) {
      setIsEditing(false);
    }
  };

  return (
    <>
      {!isEditing && (
        <>
          <span className={styles.editInPlaceText}>{initialText}</span>
          {enabled && (
            <Button
              data-testid="edit-in-place-edit-button"
              type="link"
              onClick={toggleEditing}
              icon={<EditOutlined />}
            />
          )}
        </>
      )}
      {isEditing && (
        <Form
          onFinish={onFinish}
          initialValues={{ value: initialText }}
          layout="inline"
          className={styles.editInPlaceForm}
        >
          <Form.Item
            validateStatus={isErred ? "error" : ""}
            name="value"
            help={isErred ? errorMessage : null}
          >
            <Input maxLength={49} required />
          </Form.Item>
          <Button
            data-testid="edit-in-place-submit-button"
            type="link"
            htmlType="submit"
            icon={<CheckOutlined />}
          />
          <Button
            type="link"
            onClick={toggleEditing}
            icon={<CloseOutlined />}
          />
        </Form>
      )}
    </>
  );
};

export default EditInPlace;
