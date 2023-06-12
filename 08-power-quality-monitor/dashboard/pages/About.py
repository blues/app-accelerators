import streamlit as st

st.markdown(
"""
# Blues Power Monitoring Demo!

This demo pulls data from Snowflake that was routed from [this Notehub project](https://notehub.io/project/app:eb43a9ae-0b78-4508-93c2-d39dc511fb70).

The application in question is a Notecard and Notecarrier-F-based device. The Swan-powered host
application takes readings from a connected Dr. Wattson device that monitors
power through a connected supply and sends those readings to the Notecard.

Raw JSON is routed to Snowflake using the Snowflake SQL API and transformed into
a structured data tables using views, with a view for `power.qo`, `_session.qo`
events.

"""
)