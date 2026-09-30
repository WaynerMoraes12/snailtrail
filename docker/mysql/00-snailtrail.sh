#!/bin/bash
set -euo pipefail

password="${SNAILTRAIL_DB_PASSWORD:-snailtrail}"

mysql --protocol=socket -uroot -p"${MYSQL_ROOT_PASSWORD}" <<SQL
CREATE DATABASE IF NOT EXISTS snailtrail CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
CREATE USER IF NOT EXISTS 'snailtrail'@'%' IDENTIFIED BY '${password}';
GRANT ALL PRIVILEGES ON snailtrail.* TO 'snailtrail'@'%';
GRANT SELECT, SHOW VIEW ON \`${MYSQL_DATABASE}\`.* TO 'snailtrail'@'%';
FLUSH PRIVILEGES;
SQL
