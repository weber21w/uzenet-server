function $(id){ return document.getElementById(id); }

async function apiGet(path){
	const r = await fetch(path, { credentials:"include" });
	if(!r.ok) throw new Error(`HTTP ${r.status}`);
	return await r.json();
}

function setPill(id, up, text){
	const el = $(id);
	if(!el) return;
	el.textContent = text;
	el.style.color = up ? "#b9f6c9" : "#ffb2b2";
	el.style.borderColor = up ? "rgba(140,255,176,.25)" : "rgba(255,178,178,.25)";
}

function renderBoards(data){
	const wrap = $("scoreboards");
	if(!wrap) return;

	wrap.innerHTML = "";

	for(const g of (data.games || [])){
		const div = document.createElement("div");
		div.className = "boardRow";

		const left = document.createElement("div");
		left.innerHTML = `
			<div class="name">${escapeHtml(g.name || "Game")}</div>
			<div class="meta">Players: ${g.players||0} • Live: ${g.live_matches||0}</div>
		`;

		const right = document.createElement("div");
		right.className = "top";
		const top = (g.top || []).slice(0,3).map(x => `${escapeHtml(x.name)}: ${x.score}`).join("<br>");
		right.innerHTML = top || "No scores yet";

		div.appendChild(left);
		div.appendChild(right);
		wrap.appendChild(div);
	}

	$("scoresUpdated").textContent = "Updated: " + (data.updated_at || "unknown");
}

function escapeHtml(s){
	return (""+s).replace(/[&<>"']/g, c => ({
		"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#039;"
	}[c]));
}

async function refresh(){
	try{
		const health = await apiGet("/api/health");
		setPill("svcRoom", health.room_up, "Room: " + (health.room_up ? "up" : "down"));
		setPill("svcTv", health.tv_up, "TV: " + (health.tv_up ? "up" : "down"));
		setPill("svcBridge", health.bridge_up, "Bridge: " + (health.bridge_up ? "up" : "down"));
	}catch(e){
		setPill("svcRoom", false, "Room: unknown");
		setPill("svcTv", false, "TV: unknown");
		setPill("svcBridge", false, "Bridge: unknown");
	}

	try{
		const scores = await apiGet("/api/scores");
		renderBoards(scores);
	}catch(e){
		const el = $("scoreboards");
		if(el) el.innerHTML = `<div class="muted">Failed to load scoreboards</div>`;
	}

	try{
		const me = await apiGet("/api/me");
		$("navAdmin")?.classList.toggle("hidden", !(me && me.is_admin));
	}catch(e){
		$("navAdmin")?.classList.add("hidden");
	}
}

async function doLogin(code){
	const r = await fetch("/api/login", {
		method:"POST",
		headers:{ "content-type":"application/json" },
		credentials:"include",
		body: JSON.stringify({ code })
	});
	const j = await r.json().catch(_ => ({}));
	if(!r.ok) throw new Error(j.error || `HTTP ${r.status}`);
	return j;
}

function wireLogin(){
	const form = $("loginForm");
	if(!form) return;

	form.addEventListener("submit", async (ev) => {
		ev.preventDefault();
		const msg = $("loginMsg");
		const code = ($("loginCode")?.value || "").trim();

		if(msg) msg.textContent = "Signing in…";

		try{
			const j = await doLogin(code);
			if(msg) msg.textContent = `Signed in as ${j.username || "guest"}`;
			await refresh();
		}catch(e){
			if(msg) msg.textContent = "Login failed";
		}
	});
}

wireLogin();
refresh();
setInterval(refresh, 10000);
