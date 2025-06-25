// Importar mòduls necessaris
require('dotenv').config();
const express = require('express');
const path = require('path');
const cors = require('cors');
const bodyParser = require('body-parser');
const { InfluxDB, Point } = require('@influxdata/influxdb-client');
const bcrypt = require('bcryptjs');

// Inicialitzar Express
const app = express();
app.use(cors());
app.use(bodyParser.json());

// Connexió a InfluxDB
const influx = new InfluxDB({ url: process.env.INFLUX_URL, token: process.env.INFLUX_TOKEN });
const writeApi = influx.getWriteApi(process.env.INFLUX_ORG, process.env.INFLUX_BUCKET, 'ns');
const queryApi = influx.getQueryApi(process.env.INFLUX_ORG);

// Ruta GET per obtenir llistat d'usuaris
app.get('/llistat-usuaris', async (req, res) => {
    const fluxQuery = `
        from(bucket:"${process.env.INFLUX_BUCKET}")
        |> range(start: -1y)
        |> filter(fn: (r) => r._measurement == "usuaris")
    `;

    try {
        const data = await queryApi.collectRows(fluxQuery);
        res.json(data);
    } catch (error) {
        res.status(500).json({ message: "Error consultant la base de dades" });
    }
});

// Comandes per fer servir frontend HTML i el CSS
app.use(express.static(__dirname));
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Funció per generar contrasenyes aleatòries 8 caràcters aleatoris
function generarContrasenya() {
    return Math.random().toString(36).slice(-8); 
}

// Ruta POST per iniciar sessió 
app.post('/login', async (req, res) => {
    
        const { username, password } = req.body;
       
        const influx = new InfluxDB({ url: process.env.INFLUX_URL, token: process.env.INFLUX_TOKEN });
        const queryApi = influx.getQueryApi(process.env.INFLUX_ORG);

        const fluxQuery = `from(bucket:"${process.env.INFLUX_BUCKET}") 
            |> range(start: -1y) 
            |> filter(fn: (r) => r._measurement == "usuaris" and r.username == "${username}")
            |> group(columns: ["_field"])
            |> sort(columns: ["_time"], desc: true)
            |> limit(n:1)`;

            try {
                const data = await queryApi.collectRows(fluxQuery);
        
                if (data.length === 0) {
                    return res.status(404).json({ error: "Usuari no trobat" }); 
                }

            const user = {username};
            data.forEach(row => {
                user[row._field] = row._value;
            });

            console.log("Usuari processat:", user); 
            
            const validPassword = await bcrypt.compare(password, user.password);
            if (!validPassword) {
                return res.status(401).json({ error: 'Contrasenya incorrecta' }); 
            }

            const isDoctor = user.rol === "admin";

                 return res.json({ message: "Sessió iniciada", 
                    isDoctor,
                    username, 
                    user_id: user.user_id, 
                    radar_id: user.radar_id
        });
    } catch (error) {
        console.error("Error connectant amb InfluxDB:", error.message);
        return res.status(503).json({ message: 'Error del servidor, no disponible' });
    }
});

// Ruta POST per registrar pacients (només el metge pot accedir)
app.post('/registre', async (req, res) => {
    const { username, radar_id } = req.body;

    if (!username || !radar_id) {
        return res.status(400).json({ error: "Falten dades: username i radar_id són obligatoris" });
    }

    try {
        const fluxQuery = `from(bucket:"${process.env.INFLUX_BUCKET}") 
        |> range(start: -1y) 
        |> filter(fn: (r) => r._measurement == "usuaris" and r._field == "user_id")
        |> keep(columns: ["_value"])
        |> sort(columns: ["_time"], desc: true)
        |> limit(n:1) `;
       
        let user_id = 1;
        const result = await queryApi.collectRows(fluxQuery);
        if (result.length > 0) {
            user_id = parseInt(result[0]._value) + 1;
        }

            const password = generarContrasenya();
            const hashedPassword = await bcrypt.hash(password, 10);

            const point = new Point('usuaris')
                .tag('username', username)
                .intField('user_id', user_id)
                .intField('radar_id', radar_id)
                .stringField('password', hashedPassword)
                .stringField('rol', 'usuari');
                
            writeApi.writePoint(point);
            writeApi.flush();

            res.json({
                message: "Usuari registrat correctament",
                user_id,
                username,
                password, 
                radar_id
            });

        } catch(error) {
            console.error(error);
            res.status(500).json({ message: "Error processant la petició" });
        }
        });

// Ruta POST per restablir contrasenya de l'usuari
app.post('/restablir-contrasenya', async (req, res) => {
    const { metge_user, metge_pass, username, nova_password } = req.body;

    if (!username || !nova_password) {
        return res.status(400).json({ error: "Falten dades: username i nova contrasenya són obligatoris" });
    }

    try {
        const fluxQuery = `
            from(bucket: "${process.env.INFLUX_BUCKET}")
            |> range(start: -1y)
            |> filter(fn: (r) => r._measurement == "usuaris" and r.username == "${username}")
        `;

        const data = await queryApi.collectRows(fluxQuery);
        if (data.length === 0) {
            return res.status(404).json({ error: "Usuari no trobat" });
        }

        let user = {};
        data.forEach(row => {
            user[row._field] = row._value;
        });

        const hashedPassword = await bcrypt.hash(nova_password, 10);

        const point = new Point('usuaris')
            .tag('username', username)
            .intField('user_id', user.user_id)
            .intField('radar_id', user.radar_id)
            .stringField('password', hashedPassword)
            .stringField('usuari', rol || 'usuari');

        writeApi.writePoint(point);
        await writeApi.flush();

        res.json({ message: "Contrasenya restablerta correctament", username, nova_password });

    } catch (error) {
        console.error(error);
        res.status(500).json({ error: "Error del servidor" });
    }
});
// Iniciar el servidor
app.listen(3001, () => {
    console.log('Servidor en funcionament a http://localhost:3001');
});
