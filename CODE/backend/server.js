const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const { Pool } = require('pg');

const app = express();
app.use(cors());
app.use(bodyParser.json());
app.use(bodyParser.urlencoded({ extended: true }));

// ================= PostgreSQL =================
const pool = new Pool({
  user: 'iot_database_uhsn_wxop_user',
  host: 'dpg-d4r9l5ogjchc73bomesg-a',
  database: 'iot_database_uhsn_wxop',
  password: 'ef4xs9jYCrceOnMJOkZ7KdLdDR6v2sXr',
  port: 5432,
});

// ================= INIT TABLES =================
async function initDB() {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS sensor_data (
      id SERIAL PRIMARY KEY,
      soil INT, temp FLOAT, hum FLOAT, flow INT,
      time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );`);
  await pool.query(`
    CREATE TABLE IF NOT EXISTS system_status (
      id SERIAL PRIMARY KEY,
      mode VARCHAR(20) DEFAULT 'AUTO',
      pump BOOLEAN DEFAULT FALSE,
      min_val INT DEFAULT 30,
      max_val INT DEFAULT 70,
      next_time INT DEFAULT 0,
      pump_power INT DEFAULT 36,
      schedules JSON DEFAULT '[]',
      time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );`);
  await pool.query(`
    CREATE TABLE IF NOT EXISTS command_queue (
      id SERIAL PRIMARY KEY,
      pump BOOLEAN, mode VARCHAR(20),
      pump_power INT, schedules JSON,
      status VARCHAR(20) DEFAULT 'pending',
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    );`);
  console.log("✅ Database ready");
}
initDB().catch(console.error);

// ================= API FROM ESP =================
app.post('/api/save', async (req, res) => {
  const { soil, temp, hum, flow, mode, min, max, next, pump_power, schedule } = req.body;
  try {
    await pool.query('INSERT INTO sensor_data (soil,temp,hum,flow) VALUES ($1,$2,$3,$4)', [soil, temp, hum, flow]);
    let scheduleData = [];
    if (schedule) try { scheduleData = JSON.parse(schedule); } catch { scheduleData = []; }
    await pool.query(
      `INSERT INTO system_status (mode,pump,min_val,max_val,next_time,pump_power,schedules)
       VALUES ($1,$2,$3,$4,$5,$6,$7)`,
      [mode, flow>0, min, max, next, pump_power ?? 36, scheduleData]
    );
    res.json({ status:'success' });
  } catch(e){ console.error(e); res.status(500).json({status:'error'}); }
});

// ================= API UI =================
app.get('/api/data', async (_,res)=>{
  const r = await pool.query('SELECT * FROM sensor_data ORDER BY id DESC LIMIT 1');
  res.json(r.rows[0] || {});
});
app.get('/api/status', async (_,res)=>{
  const r = await pool.query('SELECT * FROM system_status ORDER BY id DESC LIMIT 1');
  res.json(r.rows[0] || {});
});
app.post('/api/control', async (req,res)=>{
  const { pump, mode, pump_power, add_schedule, remove_schedule } = req.body;
  console.log("🧭 Control received:", req.body);
  try {
    const lastData = await pool.query('SELECT * FROM command_queue ORDER BY id DESC LIMIT 1');
    const last = lastData.rows[0] || {};
    let schedules = [];
    if (last.schedules) try { schedules = JSON.parse(last.schedules); } catch { schedules = []; }
    if (add_schedule) schedules.push(add_schedule);
    if (remove_schedule !== undefined && remove_schedule < schedules.length) schedules.splice(remove_schedule,1);

    // ✅ Xử lý giá trị pump chính xác cho cả string/boolean/number
    let newPump;
    if (typeof pump === 'string') {
      newPump = pump.toLowerCase() === 'true' || pump === '1';
    } else if (typeof pump === 'number') {
      newPump = pump === 1;
    } else {
      newPump = !!pump; // ép về boolean
    }


    const newMode = mode ?? last?.mode ?? "AUTO";
    const newPumpPower = pump_power ?? last?.pump_power ?? 36;

    await pool.query(`INSERT INTO command_queue (pump,mode,pump_power,schedules) VALUES ($1,$2,$3,$4)`,
      [newPump,newMode,newPumpPower,JSON.stringify(schedules)]);

    const sysData = await pool.query('SELECT * FROM system_status ORDER BY id DESC LIMIT 1');
    const lastSys = sysData.rows[0] || {};
    await pool.query(
      `INSERT INTO system_status (mode,pump,min_val,max_val,next_time,pump_power,schedules)
       VALUES ($1,$2,$3,$4,$5,$6,$7)`,
      [newMode,newPump,lastSys.min_val ?? 30,lastSys.max_val ?? 70,lastSys.next_time ?? 0,newPumpPower,JSON.stringify(schedules)]
    );

    res.json({status:'success'});
  } catch(e){ console.error(e); res.status(500).json({status:'error'}); }
});


app.get('/api/command', async (_,res)=>{
  const r = await pool.query('SELECT * FROM command_queue ORDER BY id DESC LIMIT 1');
  const cmd = r.rows[0];
  if(!cmd) return res.send('');
  let cmdStr = `${cmd.pump?'PUMP:ON;':'PUMP:OFF;'}MODE:${cmd.mode};POWER:${cmd.pump_power};`;
  if(cmd.schedules) cmdStr += 'SCHEDULES:'+JSON.stringify(cmd.schedules)+';';
  res.send(cmdStr);
});

// ================= API LỊCH SỬ ĐIỀU KHIỂN =================
app.get('/api/history', async (_, res) => {
  try {
    const r = await pool.query(`
      SELECT id, pump, mode, pump_power, schedules, created_at
      FROM command_queue
      ORDER BY id DESC
      LIMIT 10
    `);
    res.json(r.rows);
  } catch (e) {
    console.error(e);
    res.status(500).json({ status: 'error' });
  }
});


// ================= RUN =================
app.listen(process.env.PORT||3000,()=>console.log("🚀 Server running on port 3000"));
