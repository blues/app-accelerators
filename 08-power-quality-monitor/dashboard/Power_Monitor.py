import streamlit as st
import pandas as pd
import numpy as np
import snowflake.connector

hours_in_year = 8760
elec_emission_factor = 0.85

# Initialize connection.
@st.cache_resource
def init_connection():
    return snowflake.connector.connect(**st.secrets["snowflake"])

conn = init_connection()

power_cols = ['Serial Number', 'Device ID', 'Created', 'Apparent Power', 'Frequency', 'Last Current', 'Last Power', 'Power Factor', 'Reactive Power']
device_cols = ['Device ID', 'Serial Number', 'Maximum Current', 'Maximum Power', 'Maximum Voltage', 'Maximum Power Factor', 'Maximum Reactive Power']

# Perform query of Snowflake database.
@st.cache_data(ttl=600)
def run_query(query):
    print(f'Running query: {query}')

    with conn.cursor() as cur:
        cur.execute(query)
        return cur.fetchall()

# queries to run every time we load the page
devices_query = run_query("select * from devices_vw;")

# Get data from POWER_VW based on options.
def get_data():
    if len(sort) > 0:
        # escape single quotes
        sort_clean = sort.replace("'", "''")
        query = f'select Serial_Number AS "Serial Number", Device as "Device ID", Created, APPARENT_POWER as "Apparent Power", Frequency, LAST_CURRENT as "Last Current", LAST_POWER as "Last Power", POWER_FACTOR as "Power Factor", REACTIVE_POWER as "Reactive Power" from POWER_VW where SERIAL_NUMBER = \'{sort_clean}\' AND CREATED IS NOT NULL order by CREATED desc limit {num_records};'
    else:
        query = f'select Serial_Number AS "Serial Number", Device as "Device ID", Created, APPARENT_POWER as "Apparent Power", Frequency, LAST_CURRENT as "Last Current", LAST_POWER as "Last Power", POWER_FACTOR as "Power Factor", REACTIVE_POWER as "Reactive Power" from POWER_VW WHERE CREATED IS NOT NULL order by CREATED desc limit {num_records};'
    return run_query(query)

with st.sidebar:
    # Get list of devices and serial numbers for the Sort by Device option.
    device_rows = devices_query
    device_rows = sorted(device_rows, key=lambda x: x[0])

    # Get keys from device_rows.
    device_names = [row[1] for row in device_rows]
    device_keys = [row[0] for row in device_rows]

    """
    ### Options
    """
    num_records = st.slider('Records to fetch?', 10, 1000, 100)
    sort = st.selectbox('Device',options=device_names, key=device_keys[0])
    show_map = st.checkbox('Show map?', False)
    show_charts = st.checkbox('Show charts?', True)
    show_table_data = st.checkbox('Show table data?', False)


"""
# Blues Power Monitoring Demo
"""
devices_df = pd.DataFrame(device_rows, columns=device_cols)

st.dataframe(devices_df, hide_index=True, use_container_width=True)

if show_map:
    """
    ### Power Monitor Device Locations
    """
    device_locations_query = run_query("select * from device_locations_vw;")
    locations_cols = ['Device ID', 'Serial Number', 'latitude', 'longitude']

    power_locations = pd.DataFrame(device_locations_query, columns=locations_cols)[["latitude", "longitude"]]

    st.map(power_locations)

if show_table_data or show_charts:
    # find the index of the selected device
    device_index = device_names.index(sort)
    st.write(f'### Data for {sort} ({device_keys[device_index]})')

    data = get_data()
    power_df = pd.DataFrame(data, columns=power_cols)

    kwh_per_year = round((power_df['Last Power'][0] / 1000) * hours_in_year, 2)
    last_kwh_per_year = round((power_df['Last Power'][1] / 1000) * hours_in_year, 2)

    co2_per_year = round(kwh_per_year * elec_emission_factor, 2)
    last_co2_per_year = round(last_kwh_per_year * elec_emission_factor, 2)

    col1, col2 = st.columns(2)

    col1.metric(label="Projected Annual Emissions 🌲", value=f"{co2_per_year} Kg of CO2", delta=f"{round(co2_per_year-last_co2_per_year, 2)} Kg of CO2", delta_color="inverse")
    col2.metric(label="Projected Annual Power Use 🔌", value=f"{kwh_per_year} KwH", delta=f"{(kwh_per_year-last_kwh_per_year)} KwH", delta_color="inverse")

    st.divider()

    col1, col2, col3 = st.columns(3)

    col1.metric(label="Apparent Power", value=power_df['Apparent Power'][0], delta=(power_df['Apparent Power'][0]-power_df['Apparent Power'][1]), delta_color="inverse")
    col2.metric(label="Last Power", value=f"{power_df['Last Power'][0]} Watts", delta=f"{(power_df['Last Power'][0]-power_df['Last Power'][1])} Watts", delta_color="inverse")
    col3.metric(label="Reactive Power", value=f"{power_df['Reactive Power'][0]} VAR", delta=f"{round(power_df['Reactive Power'][0]-power_df['Reactive Power'][1] ,2)} VAR", delta_color="inverse")

    if show_table_data:
       st.dataframe(power_df, hide_index=True)

    if show_charts:
        st.area_chart(data=power_df[['Apparent Power', 'Last Power','Created']], x='Created', y=['Apparent Power', 'Last Power'])
        st.area_chart(data=power_df[['Reactive Power','Created']], x='Created', y=['Reactive Power'])