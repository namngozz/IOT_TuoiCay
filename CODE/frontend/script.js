const API_BASE = "https://iot-server-yc6r.onrender.com";

// =============== SWITCH BƠM ===============
const pumpSwitch = document.getElementById('pumpSwitch');
const pumpStatusText = document.getElementById('pumpStatusText');

pumpSwitch.addEventListener('change', async () => {
  const newState = pumpSwitch.checked;
  pumpStatusText.textContent = newState ? "Đang bật..." : "Đang tắt...";
  try {
    const res = await fetch(`${API_BASE}/api/control`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pump: newState })
    });
    const data = await res.json();
    if (data.status === 'success') {
      pumpStatusText.textContent = newState ? "BẬT" : "TẮT";
    } else {
      throw new Error('Server error');
    }
  } catch (err) {
    console.error(err);
    // Chỉ rollback nếu thật sự lỗi
    pumpSwitch.checked = !newState;
    pumpStatusText.textContent = "❌ Lỗi mạng hoặc server";
  }
});


function updatePumpUI(pump){
  pumpSwitch.checked = !!pump;
  pumpStatusText.textContent = pump ? "BẬT" : "TẮT";
}

// =============== FETCH DỮ LIỆU ===============
async function fetchData() {
  try {
    const sensorRes = await fetch(`${API_BASE}/api/data`);
    const sensor = await sensorRes.json();
    const statusRes = await fetch(`${API_BASE}/api/status`);
    const status = await statusRes.json();

    document.getElementById('soil').textContent =
      sensor.soil != null ? sensor.soil + ' %' : '- %';
    
    document.getElementById('temp').textContent =
      sensor.temp != null ? sensor.temp + ' °C' : '- °C';
    
    document.getElementById('hum').textContent =
      sensor.hum != null ? sensor.hum + ' %' : '- %';
    
    document.getElementById('flow').textContent = sensor.flow ?? '-';
    
    document.getElementById('currentMode').textContent =
      status.mode ?? '-';
    
    document.getElementById('min_val').textContent =
      status.min_val != null ? status.min_val + ' %' : '- %';
    
    document.getElementById('max_val').textContent =
      status.max_val != null ? status.max_val + ' %' : '- %';
    
    document.getElementById('next_time').textContent =
      status.next_time != null ? status.next_time + ' phút' : '- phút';


    document.getElementById('pumpPower').value = status.pump_power ?? 36;
    document.getElementById('pumpPowerLabel').textContent = (status.pump_power ?? 36)+'%';
 
    updateScheduleUI(Array.isArray(status.schedules) ? status.schedules : []);

    const cmdRes = await fetch(`${API_BASE}/api/command`);
    const cmdStr = await cmdRes.text();
    document.getElementById('commandList').textContent = cmdStr || '-';
  } catch(err){ console.error(err); }
}

// =============== MODE ===============
document.getElementById('modeApply').addEventListener('click', async ()=>{
  const mode = document.getElementById('modeSelect').value;
  await fetch(`${API_BASE}/api/control`, {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ mode })
  });
});

// =============== CÔNG SUẤT ===============
const pumpPowerInput = document.getElementById('pumpPower');
pumpPowerInput.addEventListener('input', ()=> {
  document.getElementById('pumpPowerLabel').textContent = pumpPowerInput.value+'%';
});
pumpPowerInput.addEventListener('change', async ()=>{
  await fetch(`${API_BASE}/api/control`, {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ pump_power: parseInt(pumpPowerInput.value) })
  });
});

// =============== LỊCH TƯỚI ===============
function updateScheduleUI(schedules){
  const list = document.getElementById('scheduleList');
  list.innerHTML = '';
  schedules.forEach((t,i)=>{
    const div = document.createElement('div');
    div.className='d-flex justify-content-between align-items-center mb-1';
    div.innerHTML = `<span>${t.hour.toString().padStart(2,'0')}:${t.minute.toString().padStart(2,'0')}</span>
                     <button class="btn btn-sm btn-danger" onclick="removeSchedule(${i})">X</button>`;
    list.appendChild(div);
  });
}
document.getElementById('addScheduleBtn').addEventListener('click', async ()=>{
  const val = document.getElementById('scheduleTime').value;
  if(!val) return;
  const [hour,minute] = val.split(':').map(Number);
  await fetch(`${API_BASE}/api/control`,{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({add_schedule:{hour,minute}})
  });
  
});
async function removeSchedule(index){
  await fetch(`${API_BASE}/api/control`,{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({remove_schedule:index})
  });
}

// =============== LỊCH SỬ ĐIỀU KHIỂN ===============
async function fetchHistory() {
  try {
    const res = await fetch(`${API_BASE}/api/history`);
    const history = await res.json();
    const body = document.getElementById('historyBody');
    body.innerHTML = '';

    // Nếu không có dữ liệu
    if (!Array.isArray(history) || history.length === 0) {
      body.innerHTML = '<tr><td colspan="5">Chưa có dữ liệu</td></tr>';
      return;
    }

    // Duyệt qua từng bản ghi lịch sử
    history.forEach((item, index) => {
      const row = document.createElement('tr');
      row.innerHTML = `
        <td>${item.id}</td>
        <td>${item.mode ?? '-'}</td>
        <td>
          <span class="badge ${item.pump ? 'bg-success' : 'bg-danger'}">
            ${item.pump ? 'BẬT' : 'TẮT'}
          </span>
        </td>
        <td>${item.pump_power ?? '-'}%</td>
        <td>
          ${
            Array.isArray(item.schedules) && item.schedules.length > 0
              ? item.schedules
                  .map(t =>
                    `${t.hour.toString().padStart(2, '0')}:${t.minute
                      .toString()
                      .padStart(2, '0')}`
                  )
                  .join(', ')
              : '-'
          }
        </td>

        <td>${new Date(item.created_at).toLocaleString('vi-VN')}</td>
      `;
      body.appendChild(row);
    });
  } catch (err) {
    console.error("Lỗi tải lịch sử:", err);
    document.getElementById('historyBody').innerHTML =
      '<tr><td colspan="5" class="text-danger">❌ Lỗi khi tải dữ liệu</td></tr>';
  }
}

// =============== AUTO REFRESH ===============
async function refreshAll() {
  await fetchData();
  await fetchHistory();
}

// Cập nhật dữ liệu mỗi 3 giây
setInterval(refreshAll, 3000);

// Tải lần đầu
refreshAll();
