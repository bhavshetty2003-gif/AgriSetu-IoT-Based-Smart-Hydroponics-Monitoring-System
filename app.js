/* ===== Replace firebaseConfig with your project's config (you already fixed it) ===== */
const firebaseConfig = {
  apiKey: "AIzaSyBmN-CWBa4jhucWUQmunih-lZC-BvLA_WA",
  authDomain: "agri-setu.firebaseapp.com",
  databaseURL: "https://agri-setu-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "agri-setu",
  storageBucket: "agri-setu.appspot.com",
  messagingSenderId: "765026898216",
  appId: "1:765026898216:web:044ca9f71358df976e401d"
};

// init
firebase.initializeApp(firebaseConfig)
const auth = firebase.auth();
const db = firebase.database();

// paths
const latestRef = db.ref('/agrisetu/readings/latest');
const historyRef = db.ref('/agrisetu/readings/history');

// simple auth functions used by login.html
function login(email, password) {
  document.getElementById('login-msg').innerText = 'Signing in...';
  auth.signInWithEmailAndPassword(email, password)
    .then(() => {
      // redirect handled in onAuthStateChanged
    })
    .catch(err => document.getElementById('login-msg').innerText = err.message);
}

function register(email, password) {
  document.getElementById('login-msg').innerText = 'Registering...';
  auth.createUserWithEmailAndPassword(email, password)
    .then(() => {
      document.getElementById('login-msg').innerText = 'Registered. Redirecting...';
    })
    .catch(err => document.getElementById('login-msg').innerText = err.message);
}

function signOutUser() {
  auth.signOut();
}

/* Auth state handling:
   - If on login page and user becomes signed in -> go to dashboard.html
   - If on dashboard page and user signs out -> go to login.html
*/
auth.onAuthStateChanged(user => {
  const page = document.body.dataset.page;
  if (user) {
    if (page === 'login') {
      window.location.href = 'dashboard.html';
    } else if (page === 'dashboard') {
      // user on dashboard: start listeners
      startDbListeners();
    }
  } else {
    // not signed in
    if (page === 'dashboard') {
      // redirect to login
      window.location.href = 'login.html';
    }
    // if on login page, do nothing
  }
});

/* ---------- Database listeners & UI update (dashboard) ---------- */

let phChart = null;
let tdsChart = null;
let tempChart = null;
let humChart = null;
let lightChart = null;

function startDbListeners(){
  // latest
  latestRef.on('value', snap => {
    const v = snap.val();
    if (!v) return;
    updateUIWithReading(v);
  });

  // history for chart (last 20)
  historyRef.limitToLast(20).on('value', snap => {
  const data = snap.val() || {};
  const arr = Object.values(data).sort((a,b)=> a.ts - b.ts);

  const labels = arr.map(i => new Date(i.ts).toLocaleTimeString());

  updateMultiChart({value: phChart}, 'ph-chart', labels, arr.map(i=>i.ph), 'pH', 4, 9);
  updateMultiChart({value: tdsChart}, 'tds-chart', labels, arr.map(i=>i.tds), 'TDS (ppm)', 0, 2000);
  updateMultiChart({value: tempChart}, 'temp-chart', labels, arr.map(i=>i.temp), 'Temperature (°C)', 0, 50);
  updateMultiChart({value: humChart}, 'hum-chart', labels, arr.map(i=>i.hum), 'Humidity (%)', 0, 100);
  updateMultiChart({value: lightChart}, 'light-chart', labels, arr.map(i=>i.light), 'Light (lux)', 0, 20000);
});

}

function detachDbListeners(){
  latestRef.off();
  historyRef.off();
}

function updateUIWithReading(r){
  const set = (id, val) => {
    const d = document.getElementById(id + 'Dial');
    const t = document.getElementById(id + 'Text');
    if (d) d.innerText = (typeof val === 'number') ? (Number.isInteger(val) ? val : val.toFixed(2)) : val;
    if (t) t.innerText = (typeof val === 'number') ? (Number.isInteger(val) ? val : val.toFixed(2)) : val;
  };
  set('ph', r.ph);
  set('tds', r.tds);
  set('temp', r.temp);
  set('hum', r.hum);
  set('light', r.light);

  const timeText = r.ts ? new Date(r.ts).toLocaleString() : '--';
  ['ph','tds','temp','hum','light'].forEach(id => {
    const el = document.getElementById(id + '-time');
    if (el) el.innerText = timeText;
  });
}

function updateChart(labels, values){
  const ctxEl = document.getElementById('ph-chart');
  if (!ctxEl) return;
  const ctx = ctxEl.getContext('2d');
  if (!chart) {
    chart = new Chart(ctx, {
      type: 'line',
      data: { labels: labels, datasets: [{ label: 'pH', data: values, tension:0.3, borderWidth:2 }]},
      options: { responsive: true, scales: { y: { suggestedMin: 4, suggestedMax: 9 } } }
    });
  } else {
    chart.data.labels = labels;
    chart.data.datasets[0].data = values;
    chart.update();
  }
}

/* ---------- Simulate device push (useful for testing without ESP) ---------- */
function simulatePush() {
  const now = Date.now();
  const data = {
    ph: +(5 + Math.random()*2).toFixed(2),
    tds: Math.floor(300 + Math.random()*700),
    temp: +(20 + Math.random()*10).toFixed(1),
    hum: +(40 + Math.random()*30).toFixed(1),
    light: Math.floor(200 + Math.random()*1000),
    ts: now
  };

  // Write latest
  latestRef.set(data)
    .then(()=> console.log('Simulated latest OK', data))
    .catch(err => console.error('Sim latest error', err));

  // Push history entry
  historyRef.push(data)
    .then(()=> console.log('Simulated history pushed'))
    .catch(err => console.error('Sim history error', err));
}

/* ================= Crop Data ================= */
const crops = {
  lettuce: { ph:[5.5,6.5], tds:[560,840], temp:[18,24], hum:[50,70], light:[10000,15000] },
  spinach: { ph:[6.0,7.0], tds:[700,900], temp:[16,22], hum:[50,70], light:[12000,16000] },
  coriander:{ ph:[6.2,6.8], tds:[800,1000], temp:[20,25], hum:[60,80], light:[12000,18000] },
  turmeric: { ph:[5.8,6.5], tds:[900,1200], temp:[22,30], hum:[70,85], light:[15000,20000] }
};

let selectedCrop = "lettuce";

/* Crop selection */
document.querySelectorAll(".crop-card").forEach(card=>{
  card.onclick = ()=>{
    document.querySelectorAll(".crop-card").forEach(c=>c.classList.remove("active"));
    card.classList.add("active");
    selectedCrop = card.dataset.crop;
    updateIdeal();
  };
});

/* Show ideal parameters */
function updateIdeal(){
  const c = crops[selectedCrop];
  const ul = document.getElementById("idealList");
  ul.innerHTML = `
    <li>pH: ${c.ph[0]} - ${c.ph[1]}</li>
    <li>TDS: ${c.tds[0]} - ${c.tds[1]} ppm</li>
    <li>Temperature: ${c.temp[0]} - ${c.temp[1]} °C</li>
    <li>Humidity: ${c.hum[0]} - ${c.hum[1]} %</li>
    <li>Light: ${c.light[0]} - ${c.light[1]} lux</li>
  `;
}

/* Alerts */
function checkAlerts(r){
  const c = crops[selectedCrop];
  let issues = [];

  if(r.ph < c.ph[0] || r.ph > c.ph[1]) issues.push("pH out of range");
  if(r.tds < c.tds[0] || r.tds > c.tds[1]) issues.push("TDS out of range");
  if(r.temp < c.temp[0] || r.temp > c.temp[1]) issues.push("Temperature out of range");

  const box = document.getElementById("alertBox");
  if(issues.length){
    box.classList.add("alert-danger");
    box.innerText = "⚠ " + issues.join(", ");
  } else {
    box.classList.remove("alert-danger");
    box.innerText = "All parameters normal 🌱";
  }
}

/* Hook into existing update */
const originalUpdate = updateUIWithReading;
updateUIWithReading = (r)=>{
  originalUpdate(r);
  checkAlerts(r);
};

updateIdeal();

function updateMultiChart(chartRef, canvasId, labels, values, label, min, max){
  const ctxEl = document.getElementById(canvasId);
  if (!ctxEl) return;
  const ctx = ctxEl.getContext('2d');

  if (!chartRef.value) {
    chartRef.value = new Chart(ctx, {
      type: 'line',
      data: {
        labels,
        datasets: [{
          label,
          data: values,
          borderWidth: 2,
          tension: 0.3
        }]
      },
      options: {
        responsive: true,
        scales: {
          y: { suggestedMin: min, suggestedMax: max }
        }
      }
    });
  } else {
    chartRef.value.data.labels = labels;
    chartRef.value.data.datasets[0].data = values;
    chartRef.value.update();
  }
}

